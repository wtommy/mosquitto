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

int mqtt3_handle_connect(mosquitto_db *db, mqtt3_context *context)
{
	char *protocol_name;
	uint8_t protocol_version;
	uint8_t connect_flags;
	char *client_id;
	char *will_topic = NULL, *will_message = NULL;
	uint8_t will, will_retain, will_qos, clean_session;
	uint8_t username_flag, password_flag;
	char *username, *password;
	int i;
	int rc;
	
	/* Don't accept multiple CONNECT commands. */
	if(context->core.state != mosq_cs_new) return MOSQ_ERR_PROTOCOL;

	if(_mosquitto_read_string(&context->core.in_packet, &protocol_name)) return 1;
	if(!protocol_name){
		mqtt3_socket_close(context);
		return 3;
	}
	if(strcmp(protocol_name, PROTOCOL_NAME)){
		mqtt3_log_printf(MOSQ_LOG_INFO, "Invalid protocol \"%s\" in CONNECT from %s.",
				protocol_name, context->address);
		_mosquitto_free(protocol_name);
		mqtt3_socket_close(context);
		return MOSQ_ERR_PROTOCOL;
	}
	if(_mosquitto_read_byte(&context->core.in_packet, &protocol_version)) return 1;
	if(protocol_version != PROTOCOL_VERSION){
		mqtt3_log_printf(MOSQ_LOG_INFO, "Invalid protocol version %d in CONNECT from %s.",
				protocol_version, context->address);
		_mosquitto_free(protocol_name);
		mqtt3_raw_connack(context, 1);
		mqtt3_socket_close(context);
		return MOSQ_ERR_PROTOCOL;
	}

	_mosquitto_free(protocol_name);

	if(_mosquitto_read_byte(&context->core.in_packet, &connect_flags)) return 1;
	clean_session = connect_flags & 0x02;
	will = connect_flags & 0x04;
	will_qos = (connect_flags & 0x18) >> 3;
	will_retain = connect_flags & 0x20;
	username_flag = connect_flags & 0x40;
	password_flag = connect_flags & 0x80;

	if(_mosquitto_read_uint16(&context->core.in_packet, &(context->core.keepalive))) return 1;

	if(_mosquitto_read_string(&context->core.in_packet, &client_id)) return 1;

	/* Find if this client already has an entry */
	for(i=0; i<db->context_count; i++){
		if(db->contexts[i] && db->contexts[i]->core.id && !strcmp(db->contexts[i]->core.id, client_id)){
			/* Client does match. */
			if(db->contexts[i]->core.sock == -1){
				/* Client is reconnecting after a disconnect */
				/* FIXME - does anything else need to be done here? */
			}else{
				/* Client is already connected, disconnect old version */
				mqtt3_log_printf(MOSQ_LOG_ERR, "Client %s already connected, closing old connection.", client_id);
			}
			if(clean_session){
				db->contexts[i]->clean_session = true;
			}
			mqtt3_context_cleanup(db, db->contexts[i], false);
			db->contexts[i]->core.state = mosq_cs_connected;
			db->contexts[i]->address = _mosquitto_strdup(context->address);
			db->contexts[i]->core.sock = context->core.sock;
			context->core.sock = -1;
			context->core.state = mosq_cs_disconnecting;
			context = db->contexts[i];
			break;
		}
	}

	if(connect_flags & 0x04){
		context->core.will = malloc(sizeof(struct mosquitto_message));
		if(!context->core.will) return MOSQ_ERR_NOMEM;
		if(_mosquitto_read_string(&context->core.in_packet, &context->core.will->topic)) return 1;
		if(_mosquitto_read_string(&context->core.in_packet, &will_message)) return 1;
		if(will_message){
			context->core.will->payload = (uint8_t *)will_message;
			context->core.will->payloadlen = strlen(will_message);
		}else{
			context->core.will->payload = NULL;
			context->core.will->payloadlen = 0;
		}
		context->core.will->qos = will_qos;
		context->core.will->retain = will_retain;
	}

	if(username_flag){
		rc = _mosquitto_read_string(&context->core.in_packet, &username);
		if(rc == MOSQ_ERR_SUCCESS){
			if(password_flag){
				rc = _mosquitto_read_string(&context->core.in_packet, &password);
				if(rc == MOSQ_ERR_NOMEM){
					return MOSQ_ERR_NOMEM;
				}else if(rc == MOSQ_ERR_PROTOCOL){
					/* Password flag given, but no password. Ignore. */
					password_flag = 0;
				}
			}
		}else if(rc == MOSQ_ERR_NOMEM){
			return MOSQ_ERR_NOMEM;
		}else{
			/* Username flag given, but no username. Ignore. */
			username_flag = 0;
		}
	}
	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Received CONNECT from %s as %s", context->address, client_id);
	context->core.id = client_id;
	context->clean_session = clean_session;

	if(will_topic) _mosquitto_free(will_topic);
	if(will_message) _mosquitto_free(will_message);

	context->core.state = mosq_cs_connected;
	return mqtt3_raw_connack(context, 0);
}

int mqtt3_handle_disconnect(mqtt3_context *context)
{
	if(!context){
		return 1;
	}
	if(context->core.in_packet.remaining_length != 0){
		return MOSQ_ERR_PROTOCOL;
	}
	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Received DISCONNECT from %s", context->core.id);
	context->core.state = mosq_cs_disconnecting;
	mqtt3_socket_close(context);
	return 0;
}


int mqtt3_handle_subscribe(mosquitto_db *db, mqtt3_context *context)
{
	int rc = 0;
	uint16_t mid;
	char *sub;
	uint8_t qos;
	uint8_t *payload = NULL;
	uint32_t payloadlen = 0;

	if(!context) return 1;
	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Received SUBSCRIBE from %s", context->core.id);
	/* FIXME - plenty of potential for memory leaks here */

	if(_mosquitto_read_uint16(&context->core.in_packet, &mid)) return 1;

	while(context->core.in_packet.pos < context->core.in_packet.remaining_length){
		sub = NULL;
		if(_mosquitto_read_string(&context->core.in_packet, &sub)){
			if(sub) _mosquitto_free(sub);
			if(payload) _mosquitto_free(payload);
			return 1;
		}

		if(sub){
			if(_mosquitto_read_byte(&context->core.in_packet, &qos)){
				_mosquitto_free(sub);
				if(payload) _mosquitto_free(payload);
				return 1;
			}
			if(qos > 2){
				mqtt3_log_printf(MOSQ_LOG_INFO, "Invalid QoS in subscription command from %s, disconnecting.",
					context->address);
				_mosquitto_free(sub);
				if(payload) _mosquitto_free(payload);
				return 1;
			}
			if(_mosquitto_fix_sub_topic(&sub)){
				_mosquitto_free(sub);
				if(payload) _mosquitto_free(payload);
				return 1;
			}
			if(!strlen(sub)){
				mqtt3_log_printf(MOSQ_LOG_INFO, "Empty subscription string from %s, disconnecting.",
					context->address);
				_mosquitto_free(sub);
				if(payload) _mosquitto_free(payload);
				return 1;
			}
			mqtt3_log_printf(MOSQ_LOG_DEBUG, "\t%s (QoS %d)", sub, qos);
			mqtt3_sub_add(context, sub, qos, &db->subs);
	
			if(mqtt3_retain_queue(db, context, sub, qos)) rc = 1;
			_mosquitto_free(sub);
		}

		payload = _mosquitto_realloc(payload, payloadlen + 1);
		payload[payloadlen] = qos;
		payloadlen++;
	}

	if(mqtt3_raw_suback(context, mid, payloadlen, payload)) rc = 1;
	_mosquitto_free(payload);
	
	return rc;
}

int mqtt3_handle_unsubscribe(mosquitto_db *db, mqtt3_context *context)
{
	uint16_t mid;
	char *sub;

	if(!context) return 1;
	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Received UNSUBSCRIBE from %s", context->core.id);

	if(_mosquitto_read_uint16(&context->core.in_packet, &mid)) return 1;

	while(context->core.in_packet.pos < context->core.in_packet.remaining_length){
		sub = NULL;
		if(_mosquitto_read_string(&context->core.in_packet, &sub)){
			if(sub) _mosquitto_free(sub);
			return 1;
		}

		if(sub){
			mqtt3_log_printf(MOSQ_LOG_DEBUG, "\t%s", sub);
			mqtt3_sub_remove(context, sub, &db->subs);
			_mosquitto_free(sub);
		}
	}

	if(mqtt3_send_command_with_mid(context, UNSUBACK, mid, false)) return 1;

	return MOSQ_ERR_SUCCESS;
}

