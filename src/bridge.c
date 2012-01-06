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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <config.h>

#include <mosquitto.h>
#include <mosquitto_internal.h>
#include <net_mosq.h>
#include <mqtt3.h>
#include <memory_mosq.h>
#include <send_mosq.h>
#include <util_mosq.h>
#include <will_mosq.h>

#ifdef WITH_BRIDGE

int mqtt3_bridge_new(mosquitto_db *db, struct _mqtt3_bridge *bridge)
{
	int i;
	struct mosquitto *new_context = NULL;
	struct mosquitto **tmp_contexts;
	char hostname[256];
	int len;

	assert(db);
	assert(bridge);

	new_context = mqtt3_context_init(-1);
	if(!new_context){
		return MOSQ_ERR_NOMEM;
	}
	new_context->bridge = bridge;
	for(i=0; i<db->context_count; i++){
		if(db->contexts[i] == NULL){
			db->contexts[i] = new_context;
			break;
		}
	}
	if(i==db->context_count){
		db->context_count++;
		tmp_contexts = _mosquitto_realloc(db->contexts, sizeof(struct mosquitto*)*db->context_count);
		if(tmp_contexts){
			db->contexts = tmp_contexts;
			db->contexts[db->context_count-1] = new_context;
		}else{
			_mosquitto_free(new_context);
			return MOSQ_ERR_NOMEM;
		}
	}

	/* FIXME - need to check that this name isn't already in use. */
	if(bridge->clientid){
		new_context->id = _mosquitto_strdup(bridge->clientid);
	}else{
		if(!gethostname(hostname, 256)){
			len = strlen(hostname) + strlen(bridge->name) + 2;
			new_context->id = _mosquitto_malloc(len);
			if(!new_context->id){
				return MOSQ_ERR_NOMEM;
			}
			snprintf(new_context->id, len, "%s.%s", hostname, bridge->name);
		}else{
			return 1;
		}
	}
	if(!new_context->id){
		_mosquitto_free(new_context);
		return MOSQ_ERR_NOMEM;
	}
	new_context->username = new_context->bridge->username;
	new_context->password = new_context->bridge->password;

	return mqtt3_bridge_connect(db, new_context);
}

int mqtt3_bridge_connect(mosquitto_db *db, struct mosquitto *context)
{
	int rc;
	int i;
	char *notification_topic;
	int notification_topic_len;
	uint8_t notification_payload[2];

	if(!context || !context->bridge) return MOSQ_ERR_INVAL;

	context->state = mosq_cs_new;
	context->sock = -1;
	context->last_msg_in = time(NULL);
	context->last_msg_out = time(NULL);
	context->keepalive = context->bridge->keepalive;
	context->clean_session = context->bridge->clean_session;
	context->in_packet.payload = NULL;
	mqtt3_bridge_packet_cleanup(context);

	_mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "Connecting bridge %s", context->bridge->name);
	rc = _mosquitto_socket_connect(context, context->bridge->address, context->bridge->port);
	if(rc != MOSQ_ERR_SUCCESS){
		_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error creating bridge.");
		return rc;
	}

	if(context->bridge->notifications){
		notification_topic_len = strlen(context->id)+strlen("$SYS/broker/connection//state");
		notification_topic = _mosquitto_malloc(sizeof(char)*(notification_topic_len+1));
		if(!notification_topic) return MOSQ_ERR_NOMEM;

		snprintf(notification_topic, notification_topic_len+1, "$SYS/broker/connection/%s/state", context->id);
		notification_payload[0] = '0';
		notification_payload[1] = '\0';
		mqtt3_db_messages_easy_queue(db, context, notification_topic, 1, 2, (uint8_t *)&notification_payload, 1);
		rc = _mosquitto_will_set(context, true, notification_topic, 2, (uint8_t *)&notification_payload, 1, true);
		if(rc){
			_mosquitto_free(notification_topic);
			return rc;
		}
		_mosquitto_free(notification_topic);
	}

	rc = _mosquitto_send_connect(context, context->keepalive, context->clean_session);
	if(rc) return rc;

	for(i=0; i<context->bridge->topic_count; i++){
		if(context->bridge->topics[i].direction == bd_out || context->bridge->topics[i].direction == bd_both){
			if(mqtt3_sub_add(context, context->bridge->topics[i].topic, context->bridge->topics[i].qos, &db->subs)) return 1;
		}
	}

	return MOSQ_ERR_SUCCESS;
}

void mqtt3_bridge_packet_cleanup(struct mosquitto *context)
{
	struct _mosquitto_packet *packet;
	if(!context) return;

    while(context->out_packet){
		_mosquitto_packet_cleanup(context->out_packet);
		packet = context->out_packet;
		context->out_packet = context->out_packet->next;
		_mosquitto_free(packet);
	}

	_mosquitto_packet_cleanup(&(context->in_packet));
}

#endif
