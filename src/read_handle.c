/*
Copyright (c) 2009-2011 Roger Light <roger@atchoo.org>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of mosquitto nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <string.h>

#include <config.h>

#include <mqtt3.h>
#include <mqtt3_protocol.h>
#include <memory_mosq.h>
#include <util_mosq.h>

int mqtt3_packet_handle(mosquitto_db *db, int context_index)
{
	struct mosquitto *context;

	if(context_index < 0 || context_index >= db->context_count) return MOSQ_ERR_INVAL;
	context = db->contexts[context_index];
	if(!context) return MOSQ_ERR_INVAL;

	switch((context->in_packet.command)&0xF0){
		case PINGREQ:
			return mqtt3_handle_pingreq(context);
		case PINGRESP:
			return mqtt3_handle_pingresp(context);
		case PUBACK:
			return mqtt3_handle_puback(context);
		case PUBCOMP:
			return mqtt3_handle_pubcomp(context);
		case PUBLISH:
			return mqtt3_handle_publish(db, context);
		case PUBREC:
			return mqtt3_handle_pubrec(context);
		case PUBREL:
			return mqtt3_handle_pubrel(db, context);
		case CONNECT:
			return mqtt3_handle_connect(db, context_index);
		case DISCONNECT:
			return mqtt3_handle_disconnect(db, context_index);
		case SUBSCRIBE:
			return mqtt3_handle_subscribe(db, context);
		case UNSUBSCRIBE:
			return mqtt3_handle_unsubscribe(db, context);
#ifdef WITH_BRIDGE
		case CONNACK:
			return mqtt3_handle_connack(context);
		case SUBACK:
			return mqtt3_handle_suback(context);
		case UNSUBACK:
			return mqtt3_handle_unsuback(context);
#endif
		default:
			/* If we don't recognise the command, return an error straight away. */
			return MOSQ_ERR_PROTOCOL;
	}
}

int mqtt3_handle_puback(struct mosquitto *context)
{
	uint16_t mid;

	if(!context){
		return MOSQ_ERR_INVAL;
	}
#ifdef WITH_STRICT_PROTOCOL
	if(context->in_packet.remaining_length != 2){
		return MOSQ_ERR_PROTOCOL;
	}
#endif
	if(_mosquitto_read_uint16(&context->in_packet, &mid)) return 1;
	_mosquitto_log_printf(NULL, MOSQ_LOG_DEBUG, "Received PUBACK from %s (Mid: %d)", context->id, mid);

	if(mid){
		if(mqtt3_db_message_delete(context, mid, mosq_md_out)) return 1;
	}
	return MOSQ_ERR_SUCCESS;
}

int mqtt3_handle_pingreq(struct mosquitto *context)
{
	if(!context){
		return MOSQ_ERR_INVAL;
	}
#ifdef WITH_STRICT_PROTOCOL
	if(context->in_packet.remaining_length != 0){
		return MOSQ_ERR_PROTOCOL;
	}
#endif
	_mosquitto_log_printf(NULL, MOSQ_LOG_DEBUG, "Received PINGREQ from %s", context->id);
	return mqtt3_raw_pingresp(context);
}

int mqtt3_handle_pingresp(struct mosquitto *context)
{
	if(!context){
		return MOSQ_ERR_INVAL;
	}
#ifdef WITH_STRICT_PROTOCOL
	if(context->in_packet.remaining_length != 0){
		return MOSQ_ERR_PROTOCOL;
	}
#endif
	_mosquitto_log_printf(NULL, MOSQ_LOG_DEBUG, "Received PINGRESP from %s", context->id);
	return MOSQ_ERR_SUCCESS;
}

int mqtt3_handle_pubcomp(struct mosquitto *context)
{
	uint16_t mid;

	if(!context){
		return MOSQ_ERR_INVAL;
	}
#ifdef WITH_STRICT_PROTOCOL
	if(context->in_packet.remaining_length != 2){
		return MOSQ_ERR_PROTOCOL;
	}
#endif

	if(_mosquitto_read_uint16(&context->in_packet, &mid)) return 1;
	_mosquitto_log_printf(NULL, MOSQ_LOG_DEBUG, "Received PUBCOMP from %s (Mid: %d)", context->id, mid);

	if(mid){
		if(mqtt3_db_message_delete(context, mid, mosq_md_out)) return 1;
	}
	return MOSQ_ERR_SUCCESS;
}

int mqtt3_handle_publish(mosquitto_db *db, struct mosquitto *context)
{
	char *topic;
	uint8_t *payload = NULL;
	uint32_t payloadlen;
	uint8_t dup, qos, retain;
	uint16_t mid = 0;
	int rc = 0;
	uint8_t header = context->in_packet.command;
	int res = 0;
	struct mosquitto_msg_store *stored = NULL;
	int len;
	char *topic_mount;

	dup = (header & 0x08)>>3;
	qos = (header & 0x06)>>1;
	retain = (header & 0x01);

	if(_mosquitto_read_string(&context->in_packet, &topic)) return 1;
	if(_mosquitto_fix_sub_topic(&topic)) return 1;
	if(!strlen(topic)){
		return 1;
	}
	if(_mosquitto_wildcard_check(topic)){
		/* Invalid publish topic, just swallow it. */
		_mosquitto_free(topic);
		return MOSQ_ERR_SUCCESS;
	}

	if(qos > 0){
		if(_mosquitto_read_uint16(&context->in_packet, &mid)){
			_mosquitto_free(topic);
			return 1;
		}
	}

	payloadlen = context->in_packet.remaining_length - context->in_packet.pos;
	if(context->listener && context->listener->mount_point){
		len = strlen(context->listener->mount_point) + strlen(topic) + 1;
		topic_mount = _mosquitto_calloc(len, sizeof(char));
		if(!topic_mount){
			_mosquitto_free(topic);
			return MOSQ_ERR_NOMEM;
		}
		snprintf(topic_mount, len, "%s%s", context->listener->mount_point, topic);
		_mosquitto_free(topic);
		topic = topic_mount;
	}

	_mosquitto_log_printf(NULL, MOSQ_LOG_DEBUG, "Received PUBLISH from %s (d%d, q%d, r%d, m%d, '%s', ... (%ld bytes))", context->id, dup, qos, retain, mid, topic, (long)payloadlen);
	if(payloadlen){
		payload = _mosquitto_calloc(payloadlen+1, sizeof(uint8_t));
		if(_mosquitto_read_bytes(&context->in_packet, payload, payloadlen)){
			_mosquitto_free(topic);
			return 1;
		}
	}

	/* Check for topic access */
	rc = mosquitto_acl_check(db, context, topic, MOSQ_ACL_WRITE);
	if(rc == MOSQ_ERR_ACL_DENIED){
		_mosquitto_free(topic);
		if(payload) _mosquitto_free(payload);
		return MOSQ_ERR_SUCCESS;
	}else if(rc != MOSQ_ERR_SUCCESS){
		_mosquitto_free(topic);
		if(payload) _mosquitto_free(payload);
		return rc;
	}

	if(qos > 0){
		mqtt3_db_message_store_find(context, mid, &stored);
	}
	if(!stored){
		dup = 0;
		if(mqtt3_db_message_store(db, context->id, mid, topic, qos, payloadlen, payload, retain, &stored, 0)){
			_mosquitto_free(topic);
			if(payload) _mosquitto_free(payload);
			return 1;
		}
	}else{
		dup = 1;
	}
	switch(qos){
		case 0:
			if(mqtt3_db_messages_queue(db, context->id, topic, qos, retain, stored)) rc = 1;
			break;
		case 1:
			if(mqtt3_db_messages_queue(db, context->id, topic, qos, retain, stored)) rc = 1;
			if(mqtt3_raw_puback(context, mid)) rc = 1;
			break;
		case 2:
			if(!dup){
				res = mqtt3_db_message_insert(context, mid, mosq_md_in, qos, retain, stored);
			}else{
				res = 0;
			}
			if(!res){
				if(mqtt3_raw_pubrec(context, mid)) rc = 1;
			}else if(res == 1){
				rc = 1;
			}
			break;
	}
	_mosquitto_free(topic);
	if(payload) _mosquitto_free(payload);

	return rc;
}

int mqtt3_handle_pubrec(struct mosquitto *context)
{
	uint16_t mid;

	if(!context){
		return MOSQ_ERR_INVAL;
	}
	if(context->in_packet.remaining_length != 2){
		return MOSQ_ERR_NOMEM;
	}
	if(_mosquitto_read_uint16(&context->in_packet, &mid)) return 1;
	_mosquitto_log_printf(NULL, MOSQ_LOG_DEBUG, "Received PUBREC from %s (Mid: %d)", context->id, mid);

	if(mqtt3_db_message_update(context, mid, mosq_md_out, ms_wait_pubcomp)) return 1;
	if(mqtt3_raw_pubrel(context, mid, false)) return 1;

	return MOSQ_ERR_SUCCESS;
}

int mqtt3_handle_pubrel(mosquitto_db *db, struct mosquitto *context)
{
	uint16_t mid;

	if(!context){
		return MOSQ_ERR_INVAL;
	}
	if(context->in_packet.remaining_length != 2){
		return MOSQ_ERR_NOMEM;
	}
	if(_mosquitto_read_uint16(&context->in_packet, &mid)) return 1;
	_mosquitto_log_printf(NULL, MOSQ_LOG_DEBUG, "Received PUBREL from %s (Mid: %d)", context->id, mid);

	mqtt3_db_message_release(db, context, mid, mosq_md_in);
	if(mqtt3_raw_pubcomp(context, mid)) return 1;

	return MOSQ_ERR_SUCCESS;
}

