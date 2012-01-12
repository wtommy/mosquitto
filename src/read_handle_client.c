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

#include <config.h>
#include <stdio.h>
#include <string.h>

#include <mqtt3.h>
#include <memory_mosq.h>
#include <send_mosq.h>
#include <util_mosq.h>

int mqtt3_handle_connack(mosquitto_db *db, struct mosquitto *context)
{
	uint8_t byte;
	uint8_t rc;
	int i;
	char *notification_topic;
	int notification_topic_len;
	uint8_t notification_payload[2];

	if(!context){
		return MOSQ_ERR_INVAL;
	}
#ifdef WITH_STRICT_PROTOCOL
	if(context->in_packet.remaining_length != 2){
		return MOSQ_ERR_PROTOCOL;
	}
#endif
	_mosquitto_log_printf(NULL, MOSQ_LOG_DEBUG, "Received CONNACK on connection %s.", context->id);
	if(_mosquitto_read_byte(&context->in_packet, &byte)) return 1; // Reserved byte, not used
	if(_mosquitto_read_byte(&context->in_packet, &rc)) return 1;
	switch(rc){
		case 0:
			if(context->bridge){
				if(context->bridge->notifications){
					notification_topic_len = strlen(context->id)+strlen("$SYS/broker/connection//state");
					notification_topic = _mosquitto_malloc(sizeof(char)*(notification_topic_len+1));
					if(!notification_topic) return MOSQ_ERR_NOMEM;

					snprintf(notification_topic, notification_topic_len+1, "$SYS/broker/connection/%s/state", context->id);
					notification_payload[0] = '1';
					notification_payload[1] = '\0';
					if(_mosquitto_send_real_publish(context, _mosquitto_mid_generate(context),
							notification_topic, 2, (uint8_t *)&notification_payload, 1, true, 0)){

						_mosquitto_free(notification_topic);
						return 1;
					}
					mqtt3_db_messages_easy_queue(db, context, notification_topic, 1, 2, (uint8_t *)&notification_payload, 1);
					_mosquitto_free(notification_topic);
				}
				for(i=0; i<context->bridge->topic_count; i++){
					if(context->bridge->topics[i].direction == bd_in || context->bridge->topics[i].direction == bd_both){
						if(_mosquitto_send_subscribe(context, NULL, false, context->bridge->topics[i].topic, 2)){
							return 1;
						}
					}
				}
			}
			context->state = mosq_cs_connected;
			return MOSQ_ERR_SUCCESS;
		case 1:
			_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Connection Refused: unacceptable protocol version");
			return 1;
		case 2:
			_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Connection Refused: identifier rejected");
			return 1;
		case 3:
			_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Connection Refused: broker unavailable");
			return 1;
	}
	return 1;
}

