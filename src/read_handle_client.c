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

#include <config.h>
#include <mqtt3.h>

void (*client_connack_callback)(int) = NULL;

int mqtt3_handle_connack(mqtt3_context *context)
{
	uint8_t byte;
	uint8_t rc;
	int i;

	if(!context || context->core.in_packet.remaining_length != 2){
		return 1;
	}
	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Received CONNACK");
	if(_mosquitto_read_byte(&context->core.in_packet, &byte)) return 1; // Reserved byte, not used
	if(_mosquitto_read_byte(&context->core.in_packet, &rc)) return 1;
	if(client_connack_callback){
		client_connack_callback(rc);
	}
	switch(rc){
		case 0:
			if(context->bridge){
				for(i=0; i<context->bridge->topic_count; i++){
					if(context->bridge->topics[i].direction == bd_in || context->bridge->topics[i].direction == bd_both){
						if(mqtt3_raw_subscribe(context, false, context->bridge->topics[i].topic, 2)){
							return 1;
						}
					}
				}
			}
			context->core.state = mosq_cs_connected;
			return 0;
		case 1:
			mqtt3_log_printf(MOSQ_LOG_ERR, "Connection Refused: unacceptable protocol version");
			return 1;
		case 2:
			mqtt3_log_printf(MOSQ_LOG_ERR, "Connection Refused: identifier rejected");
			return 1;
		case 3:
			mqtt3_log_printf(MOSQ_LOG_ERR, "Connection Refused: broker unavailable");
			return 1;
	}
	return 1;
}

int mqtt3_handle_suback(mqtt3_context *context)
{
	uint16_t mid;
	uint8_t granted_qos;

	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Received SUBACK");
	if(_mosquitto_read_uint16(&context->core.in_packet, &mid)) return 1;

	while(context->core.in_packet.pos < context->core.in_packet.remaining_length){
		/* FIXME - Need to do something with this */
		if(_mosquitto_read_byte(&context->core.in_packet, &granted_qos)) return 1;
	}

	return 0;
}

int mqtt3_handle_unsuback(mqtt3_context *context)
{
	uint16_t mid;

	if(!context || context->core.in_packet.remaining_length != 2){
		return 1;
	}
	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Received UNSUBACK");
	if(_mosquitto_read_uint16(&context->core.in_packet, &mid)) return 1;

	return 0;
}
