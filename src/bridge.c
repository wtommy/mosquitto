/*
Copyright (c) 2009,2010 Roger Light <roger@atchoo.org>
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
#include <stdint.h>

#include <config.h>
#include <net_mosq.h>
#include <mqtt3.h>
#include <memory_mosq.h>

int mqtt3_bridge_new(mosquitto_db *db, struct _mqtt3_bridge *bridge)
{
	int i;
	mqtt3_context *new_context = NULL;
	mqtt3_context **tmp_contexts;

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
		tmp_contexts = _mosquitto_realloc(db->contexts, sizeof(mqtt3_context*)*db->context_count);
		if(tmp_contexts){
			db->contexts = tmp_contexts;
			db->contexts[db->context_count-1] = new_context;
		}else{
			_mosquitto_free(new_context);
			return MOSQ_ERR_NOMEM;
		}
	}

	/* FIXME - need to check that this name isn't already in use. */
	new_context->core.id = _mosquitto_strdup(bridge->name);
	if(!new_context->core.id){
		_mosquitto_free(new_context);
		return MOSQ_ERR_NOMEM;
	}
	new_context->core.username = new_context->bridge->username;
	new_context->core.password = new_context->bridge->password;

	return mqtt3_bridge_connect(db, new_context);
}

int mqtt3_bridge_connect(mosquitto_db *db, mqtt3_context *context)
{
	int new_sock = -1;
	int i;

	if(!context || !context->bridge) return 1;

	context->core.state = mosq_cs_new;
	context->duplicate = false;
	context->core.sock = -1;
	context->core.last_msg_in = time(NULL);
	context->core.last_msg_out = time(NULL);
	context->core.keepalive = context->bridge->keepalive;
	context->clean_session = context->bridge->clean_session;
	context->core.in_packet.payload = NULL;
	mqtt3_bridge_packet_cleanup(context);

	mqtt3_log_printf(MOSQ_LOG_NOTICE, "Connecting bridge %s", context->bridge->name);
	new_sock = _mosquitto_socket_connect(context->bridge->address, context->bridge->port);
	if(new_sock == -1){
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error creating bridge.");
		return 1;
	}

	context->core.sock = new_sock;

	context->core.last_msg_in = time(NULL);
	if(mqtt3_raw_connect(context, context->core.id,
			/*will*/ false, /*will qos*/ 0, /*will retain*/ false, /*will topic*/ NULL, /*will msg*/ NULL,
			context->core.keepalive, context->clean_session)){

		return 1;
	}

	for(i=0; i<context->bridge->topic_count; i++){
		if(context->bridge->topics[i].direction == bd_out || context->bridge->topics[i].direction == bd_both){
			if(mqtt3_sub_add(context, context->bridge->topics[i].topic, 2, &db->subs)) return 1;
		}
	}

	return 0;
}

void mqtt3_bridge_packet_cleanup(mqtt3_context *context)
{
	struct _mosquitto_packet *packet;
	if(!context) return;

    while(context->core.out_packet){
		_mosquitto_packet_cleanup(context->core.out_packet);
		packet = context->core.out_packet;
		context->core.out_packet = context->core.out_packet->next;
		_mosquitto_free(packet);
	}

	_mosquitto_packet_cleanup(&(context->core.in_packet));
}
