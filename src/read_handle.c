/*
Copyright (c) 2009,2010, Roger Light <roger@atchoo.org>
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

#include <string.h>

#include <config.h>
#include <mqtt3.h>
#include <mqtt3_protocol.h>
#include <memory_mosq.h>
#include <util_mosq.h>

int mqtt3_packet_handle(mosquitto_db *db, mqtt3_context *context)
{
	if(!context) return 1;

	switch((context->core.in_packet.command)&0xF0){
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
#ifdef WITH_BROKER
		case CONNECT:
			return mqtt3_handle_connect(db, context);
		case DISCONNECT:
			return mqtt3_handle_disconnect(context);
		case SUBSCRIBE:
			return mqtt3_handle_subscribe(context);
		case UNSUBSCRIBE:
			return mqtt3_handle_unsubscribe(context);
#endif
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

int mqtt3_handle_puback(mqtt3_context *context)
{
	uint16_t mid;

	if(!context){
		return 1;
	}
	if(context->core.in_packet.remaining_length != 2){
		return MOSQ_ERR_PROTOCOL;
	}
	if(_mosquitto_read_uint16(&context->core.in_packet, &mid)) return 1;
	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Received PUBACK from %s (Mid: %d)", context->core.id, mid);

	if(mid){
		if(mqtt3_db_message_delete(context, mid, mosq_md_out)) return 1;
	}
	return MOSQ_ERR_SUCCESS;
}

int mqtt3_handle_pingreq(mqtt3_context *context)
{
	if(!context){
		return 1;
	}
	if(context->core.in_packet.remaining_length != 0){
		return MOSQ_ERR_PROTOCOL;
	}
	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Received PINGREQ from %s", context->core.id);
	return mqtt3_raw_pingresp(context);
}

int mqtt3_handle_pingresp(mqtt3_context *context)
{
	if(!context){
		return 1;
	}
	if(context->core.in_packet.remaining_length != 0){
		return MOSQ_ERR_PROTOCOL;
	}
	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Received PINGRESP from %s", context->core.id);
	return MOSQ_ERR_SUCCESS;
}

int mqtt3_handle_pubcomp(mqtt3_context *context)
{
	uint16_t mid;

	if(!context){
		return 1;
	}
	if(context->core.in_packet.remaining_length != 2){
		return MOSQ_ERR_PROTOCOL;
	}
	if(_mosquitto_read_uint16(&context->core.in_packet, &mid)) return 1;
	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Received PUBCOMP from %s (Mid: %d)", context->core.id, mid);

	if(mid){
		if(mqtt3_db_message_delete(context, mid, mosq_md_out)) return 1;
	}
	return MOSQ_ERR_SUCCESS;
}

int mqtt3_handle_publish(mosquitto_db *db, mqtt3_context *context)
{
	char *topic;
	uint8_t *payload = NULL;
	uint32_t payloadlen;
	uint8_t dup, qos, retain;
	uint16_t mid = 0;
	int rc = 0;
	uint8_t header = context->core.in_packet.command;
	int res;
	struct mosquitto_msg_store *stored;

	dup = (header & 0x08)>>3;
	qos = (header & 0x06)>>1;
	retain = (header & 0x01);

	if(_mosquitto_read_string(&context->core.in_packet, &topic)) return 1;
	if(_mosquitto_fix_sub_topic(&topic)) return 1;
	if(!strlen(topic)){
		return 1;
	}

	if(qos > 0){
		if(_mosquitto_read_uint16(&context->core.in_packet, &mid)){
			_mosquitto_free(topic);
			return 1;
		}
	}

	payloadlen = context->core.in_packet.remaining_length - context->core.in_packet.pos;
	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Received PUBLISH from %s (%d, %d, %d, %d, '%s', ... (%ld bytes))", context->core.id, dup, qos, retain, mid, topic, (long)payloadlen);
	if(payloadlen){
		payload = _mosquitto_calloc(payloadlen+1, sizeof(uint8_t));
		if(_mosquitto_read_bytes(&context->core.in_packet, payload, payloadlen)){
			_mosquitto_free(topic);
			return 1;
		}
	}else{
		if(retain){
			/* If retain is set and we have a zero payloadlen, delete the
			 * retained message because the last message was a null! */
			rc = mqtt3_db_retain_delete(topic);
		}
	}

	if(mqtt3_db_message_store(db, context->core.id, mid, topic, qos, payloadlen, payload, retain, &stored)){
		_mosquitto_free(topic);
		if(payload) _mosquitto_free(payload);
		return 1;
	}
	switch(qos){
		case 0:
			if(mqtt3_db_messages_queue(db, context->core.id, topic, qos, retain, stored)) rc = 1;
			break;
		case 1:
			if(mqtt3_db_messages_queue(db, context->core.id, topic, qos, retain, stored)) rc = 1;
			if(mqtt3_raw_puback(context, mid)) rc = 1;
			break;
		case 2:
			res = mqtt3_db_message_insert(context, mid, mosq_md_in, ms_wait_pubrec, qos, stored);
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

int mqtt3_handle_pubrec(mqtt3_context *context)
{
	uint16_t mid;

	if(!context){
		return 1;
	}
	if(context->core.in_packet.remaining_length != 2){
		return MOSQ_ERR_NOMEM;
	}
	if(_mosquitto_read_uint16(&context->core.in_packet, &mid)) return 1;
	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Received PUBREC from %s (Mid: %d)", context->core.id, mid);

	if(mqtt3_db_message_update(context, mid, mosq_md_out, ms_wait_pubcomp)) return 1;
	if(mqtt3_raw_pubrel(context, mid)) return 1;

	return MOSQ_ERR_SUCCESS;
}

int mqtt3_handle_pubrel(mosquitto_db *db, mqtt3_context *context)
{
	uint16_t mid;

	if(!context){
		return 1;
	}
	if(context->core.in_packet.remaining_length != 2){
		return MOSQ_ERR_NOMEM;
	}
	if(_mosquitto_read_uint16(&context->core.in_packet, &mid)) return 1;
	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Received PUBREL from %s (Mid: %d)", context->core.id, mid);

	if(mqtt3_db_message_release(db, context, mid, mosq_md_in)) return 1;
	if(mqtt3_raw_pubcomp(context, mid)) return 1;

	return MOSQ_ERR_SUCCESS;
}

