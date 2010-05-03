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

#include <stdint.h>

#include <config.h>
#include <mqtt3.h>

int mqtt3_bridge_new(mqtt3_context **contexts, int *context_count, struct _mqtt3_bridge *bridge)
{
	int i;
	mqtt3_context *new_context = NULL;
	mqtt3_context **tmp_contexts;

	if(!contexts || !bridge) return 1;

	new_context = mqtt3_context_init(-1);
	if(!new_context){
		return 1;
	}
	new_context->bridge = bridge;
	for(i=0; i<(*context_count); i++){
		if(contexts[i] == NULL){
			contexts[i] = new_context;
			break;
		}
	}
	if(i==(*context_count)){
		(*context_count)++;
		tmp_contexts = mqtt3_realloc(contexts, sizeof(mqtt3_context*)*(*context_count));
		if(tmp_contexts){
			contexts = tmp_contexts;
			contexts[(*context_count)-1] = new_context;
		}
	}

	new_context->id = mqtt3_strdup(bridge->name);
	if(!mqtt3_db_client_insert(new_context, 0, 0, 0, NULL, NULL)){
		return mqtt3_bridge_connect(new_context);
	}
	return 1;
}

int mqtt3_bridge_connect(mqtt3_context *context)
{
	int new_sock = -1;
	int i;

	if(!context || !context->bridge) return 1;

	new_sock = mqtt3_socket_connect(context->bridge->address, context->bridge->port);
	if(new_sock == -1){
		mqtt3_log_printf(MQTT3_LOG_ERR, "Error creating bridge.");
		return 1;
	}

	context->sock = new_sock;

	context->connected = false;
	context->disconnecting = false;
	context->last_msg_in = time(NULL);
	mqtt3_db_client_update(context, 0, 0, 0, NULL, NULL);
	if(mqtt3_raw_connect(context, context->id,
			/*will*/ false, /*will qos*/ 0, /*will retain*/ false, /*will topic*/ NULL, /*will msg*/ NULL,
			60/*keepalive*/, /*cleanstart*/true)){

		return 1;
	}

	for(i=0; i<context->bridge->topic_count; i++){
		if(context->bridge->topics[i].direction == bd_out || context->bridge->topics[i].direction == bd_both){
			if(mqtt3_db_sub_insert(context->id, context->bridge->topics[i].topic, 2)) return 1;
		}
	}

	return 0;
}

void mqtt3_bridge_packet_cleanup(mqtt3_context *context)
{
	if(!context) return;

    while(context->out_packet){
		mqtt3_context_packet_cleanup(context->out_packet);
		context->out_packet = context->out_packet->next;
	}

	mqtt3_context_packet_cleanup(&(context->in_packet));
}
