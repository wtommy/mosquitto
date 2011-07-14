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
#include <send_mosq.h>
#include <util_mosq.h>

int mqtt3_handle_connect(mosquitto_db *db, int context_index)
{
	char *protocol_name;
	uint8_t protocol_version;
	uint8_t connect_flags;
	char *client_id;
	char *will_message = NULL, *will_topic = NULL;
	struct mosquitto_message *will_struct = NULL;
	uint8_t will, will_retain, will_qos, clean_session;
	uint8_t username_flag, password_flag;
	char *username, *password = NULL;
	int i;
	int rc;
	struct _mosquitto_acl_user *acl_tail;
	mqtt3_context *context;

	context = db->contexts[context_index];
	
	/* Don't accept multiple CONNECT commands. */
	if(context->core.state != mosq_cs_new){
		mqtt3_context_disconnect(db, context_index);
		return MOSQ_ERR_PROTOCOL;
	}

	if(_mosquitto_read_string(&context->core.in_packet, &protocol_name)){
		mqtt3_context_disconnect(db, context_index);
		return 1;
	}
	if(!protocol_name){
		mqtt3_context_disconnect(db, context_index);
		return 3;
	}
	if(strcmp(protocol_name, PROTOCOL_NAME)){
		mqtt3_log_printf(MOSQ_LOG_INFO, "Invalid protocol \"%s\" in CONNECT from %s.",
				protocol_name, context->core.address);
		_mosquitto_free(protocol_name);
		mqtt3_context_disconnect(db, context_index);
		return MOSQ_ERR_PROTOCOL;
	}
	_mosquitto_free(protocol_name);

	if(_mosquitto_read_byte(&context->core.in_packet, &protocol_version)){
		mqtt3_context_disconnect(db, context_index);
		return 1;
	}
	if(protocol_version != PROTOCOL_VERSION){
		mqtt3_log_printf(MOSQ_LOG_INFO, "Invalid protocol version %d in CONNECT from %s.",
				protocol_version, context->core.address);
		_mosquitto_free(protocol_name);
		mqtt3_raw_connack(context, 1);
		mqtt3_context_disconnect(db, context_index);
		return MOSQ_ERR_PROTOCOL;
	}

	if(_mosquitto_read_byte(&context->core.in_packet, &connect_flags)){
		mqtt3_context_disconnect(db, context_index);
		return 1;
	}
	clean_session = connect_flags & 0x02;
	will = connect_flags & 0x04;
	will_qos = (connect_flags & 0x18) >> 3;
	will_retain = connect_flags & 0x20;
	password_flag = connect_flags & 0x40;
	username_flag = connect_flags & 0x80;

	if(_mosquitto_read_uint16(&context->core.in_packet, &(context->core.keepalive))){
		mqtt3_context_disconnect(db, context_index);
		return 1;
	}

	if(_mosquitto_read_string(&context->core.in_packet, &client_id)){
		mqtt3_context_disconnect(db, context_index);
		_mosquitto_free(client_id);
		return 1;
	}

	/* clientid_prefixes check */
	if(db->config->clientid_prefixes){
		if(strncmp(db->config->clientid_prefixes, client_id, strlen(db->config->clientid_prefixes))){
			_mosquitto_free(client_id);
			mqtt3_raw_connack(context, 2);
			mqtt3_context_disconnect(db, context_index);
			return MOSQ_ERR_SUCCESS;
		}
	}

	if(will){
		will_struct = _mosquitto_calloc(1, sizeof(struct mosquitto_message));
		if(!will_struct){
			_mosquitto_free(client_id);
			mqtt3_context_disconnect(db, context_index);
			return MOSQ_ERR_NOMEM;
		}
		if(_mosquitto_read_string(&context->core.in_packet, &will_topic)){
			_mosquitto_free(client_id);
			mqtt3_context_disconnect(db, context_index);
			return 1;
		}
		if(_mosquitto_read_string(&context->core.in_packet, &will_message)){
			_mosquitto_free(client_id);
			mqtt3_context_disconnect(db, context_index);
			return 1;
		}
	}

	if(username_flag){
		rc = _mosquitto_read_string(&context->core.in_packet, &username);
		if(rc == MOSQ_ERR_SUCCESS){
			if(password_flag){
				rc = _mosquitto_read_string(&context->core.in_packet, &password);
				if(rc == MOSQ_ERR_NOMEM){
					_mosquitto_free(username);
					_mosquitto_free(client_id);
					return MOSQ_ERR_NOMEM;
				}else if(rc == MOSQ_ERR_PROTOCOL){
					/* Password flag given, but no password. Ignore. */
					password_flag = 0;
				}
			}
			rc = mosquitto_unpwd_check(db, username, password);
			_mosquitto_free(username);
			_mosquitto_free(password);
			if(rc == MOSQ_ERR_AUTH){
				mqtt3_raw_connack(context, 2);
				mqtt3_context_disconnect(db, context_index);
				_mosquitto_free(client_id);
				return MOSQ_ERR_SUCCESS;
			}else if(rc == MOSQ_ERR_INVAL){
				_mosquitto_free(client_id);
				return MOSQ_ERR_INVAL;
			}
		}else if(rc == MOSQ_ERR_NOMEM){
			_mosquitto_free(client_id);
			return MOSQ_ERR_NOMEM;
		}else{
			/* Username flag given, but no username. Ignore. */
			username_flag = 0;
		}
	}

	if(!username_flag && db->config->allow_anonymous == false){
		mqtt3_raw_connack(context, 2);
		mqtt3_context_disconnect(db, context_index);
		_mosquitto_free(client_id);
		return MOSQ_ERR_SUCCESS;
	}

	/* Find if this client already has an entry. This must be done *after* any security checks. */
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
			db->contexts[i]->core.clean_session = clean_session;
			mqtt3_context_cleanup(db, db->contexts[i], false);
			db->contexts[i]->core.state = mosq_cs_connected;
			db->contexts[i]->core.address = _mosquitto_strdup(context->core.address);
			db->contexts[i]->core.sock = context->core.sock;
			db->contexts[i]->listener = context->listener;
			context->core.sock = -1;
			context->core.state = mosq_cs_disconnecting;
			context = db->contexts[i];
			break;
		}
	}

	context->core.id = client_id;
	context->core.clean_session = clean_session;

	context->core.will = will_struct;
	if(context->core.will){
		context->core.will->topic = will_topic;
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

	/* Associate user with its ACL, assuming we have ACLs loaded. */
	if(db->acl_list){
		acl_tail = db->acl_list;
		while(acl_tail){
			if(context->core.username){
				if(!strcmp(context->core.username, acl_tail->username)){
					context->acl_list = acl_tail;
					break;
				}
			}else{
				if(acl_tail->username == NULL){
					context->acl_list = acl_tail;
					break;
				}
			}
			acl_tail = acl_tail->next;
		}
	}else{
		context->acl_list = NULL;
	}

	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Received CONNECT from %s as %s", context->core.address, client_id);

	context->core.state = mosq_cs_connected;
	return mqtt3_raw_connack(context, 0);
}

int mqtt3_handle_disconnect(mosquitto_db *db, int context_index)
{
	mqtt3_context *context;

	context = db->contexts[context_index];

	if(!context){
		return MOSQ_ERR_INVAL;
	}
	if(context->core.in_packet.remaining_length != 0){
		return MOSQ_ERR_PROTOCOL;
	}
	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Received DISCONNECT from %s", context->core.id);
	context->core.state = mosq_cs_disconnecting;
	mqtt3_context_disconnect(db, context_index);
	return MOSQ_ERR_SUCCESS;
}


int mqtt3_handle_subscribe(mosquitto_db *db, mqtt3_context *context)
{
	int rc = 0;
	uint16_t mid;
	char *sub;
	uint8_t qos;
	uint8_t *payload = NULL, *tmp_payload;
	uint32_t payloadlen = 0;
	int len;
	char *sub_mount;

	if(!context) return MOSQ_ERR_INVAL;
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
					context->core.address);
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
					context->core.address);
				_mosquitto_free(sub);
				if(payload) _mosquitto_free(payload);
				return 1;
			}
			if(context->listener && context->listener->mount_point){
				len = strlen(context->listener->mount_point) + strlen(sub) + 1;
				sub_mount = _mosquitto_calloc(len, sizeof(char));
				if(!sub_mount){
					_mosquitto_free(sub);
					if(payload) _mosquitto_free(payload);
					return MOSQ_ERR_NOMEM;
				}
				snprintf(sub_mount, len, "%s%s", context->listener->mount_point, sub);
				_mosquitto_free(sub);
				sub = sub_mount;

			}
			mqtt3_log_printf(MOSQ_LOG_DEBUG, "\t%s (QoS %d)", sub, qos);
			/* FIXME - need to deny access to retained messages. */
#if 0
			/* Check for topic access */
			rc2 = mqtt3_acl_check(db, context, sub, MOSQ_ACL_READ);
			if(rc2 == MOSQ_ERR_SUCCESS){
				mqtt3_sub_add(context, sub, qos, &db->subs);
				if(mqtt3_retain_queue(db, context, sub, qos)) rc = 1;
			}else if(rc2 != MOSQ_ERR_ACL_DENIED){
				rc = 1;
			}
#else
			mqtt3_sub_add(context, sub, qos, &db->subs);
			if(mqtt3_retain_queue(db, context, sub, qos)) rc = 1;
#endif
			_mosquitto_free(sub);
		}

		tmp_payload = _mosquitto_realloc(payload, payloadlen + 1);
		if(tmp_payload){
			payload = tmp_payload;
			payload[payloadlen] = qos;
			payloadlen++;
		}else{
			if(payload) _mosquitto_free(payload);

			return MOSQ_ERR_NOMEM;
		}
	}

	if(mqtt3_raw_suback(context, mid, payloadlen, payload)) rc = 1;
	_mosquitto_free(payload);
	
	return rc;
}

int mqtt3_handle_unsubscribe(mosquitto_db *db, mqtt3_context *context)
{
	uint16_t mid;
	char *sub;

	if(!context) return MOSQ_ERR_INVAL;
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

	if(_mosquitto_send_command_with_mid(&context->core, UNSUBACK, mid, false)) return 1;

	return MOSQ_ERR_SUCCESS;
}

