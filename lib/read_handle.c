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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>

#include <mosquitto.h>
#include <mqtt3_protocol.h>
#include <read_handle.h>
#include <net_mosq.h>
#include <send_mosq.h>
#include <util_mosq.h>

int _mosquitto_packet_handle(struct mosquitto *mosq)
{
	if(!mosq) return 1;

	switch((mosq->in_packet.command)&0xF0){
		case PINGREQ:
			return _mosquitto_handle_pingreq(mosq);
		case PINGRESP:
			return _mosquitto_handle_pingresp(mosq);
		// FIXME case PUBACK:
			// FIXME return mqtt3_handle_puback(context);
		// FIXME case PUBCOMP:
			// FIXME return mqtt3_handle_pubcomp(context);
		case PUBLISH:
			return _mosquitto_handle_publish(mosq);
		// FIXME case PUBREC:
			// FIXME return mqtt3_handle_pubrec(context);
		// FIXME case PUBREL:
			// FIXME return mqtt3_handle_pubrel(context);
		case CONNACK:
			return _mosquitto_handle_connack(mosq);
		case SUBACK:
			return _mosquitto_handle_suback(mosq);
		case UNSUBACK:
			return _mosquitto_handle_unsuback(mosq);
		default:
			/* If we don't recognise the command, return an error straight away. */
			fprintf(stderr, "Error: Unrecognised command %d\n", (mosq->in_packet.command)&0xF0);
			return 1;
	}
}

int _mosquitto_handle_pingreq(struct mosquitto *mosq)
{
	if(!mosq || mosq->in_packet.remaining_length != 0){
		return 1;
	}
	//FIXME _mosquitto_log_printf(MQTT3_LOG_DEBUG, "Received PINGREQ from %s", mosq->id);
	return _mosquitto_send_pingresp(mosq);
}

int _mosquitto_handle_pingresp(struct mosquitto *mosq)
{
	if(!mosq || mosq->in_packet.remaining_length != 0){
		return 1;
	}
	//FIXME mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received PINGRESP from %s", mosq->id);
	return 0;
}

int _mosquitto_handle_publish(struct mosquitto *mosq)
{
	uint8_t header;
	struct mosquitto_message *message;

	if(!mosq) return 1;

	message = calloc(1, sizeof(struct mosquitto_message));
	if(!message) return 1;

	header = mosq->in_packet.command;

	//FIXME _mosquitto_log_printf(MQTT3_LOG_DEBUG, "Received PUBLISH from %s", mosq->id);
	message->dup = (header & 0x08)>>3;
	message->qos = (header & 0x06)>>1;
	message->retain = (header & 0x01);

	if(_mosquitto_read_string(&mosq->in_packet, &message->topic)) return 1;
	if(_mosquitto_fix_sub_topic(&message->topic)) return 1;
	if(!strlen(message->topic)){
		mosquitto_message_cleanup(&message);
		return 1;
	}

	if(message->qos > 0){
		if(_mosquitto_read_uint16(&mosq->in_packet, &message->mid)){
			mosquitto_message_cleanup(&message);
			return 1;
		}
	}

	message->payloadlen = mosq->in_packet.remaining_length - mosq->in_packet.pos;
	if(message->payloadlen){
		message->payload = calloc(message->payloadlen+1, sizeof(uint8_t));
		if(_mosquitto_read_bytes(&mosq->in_packet, message->payload, message->payloadlen)){
			mosquitto_message_cleanup(&message);
			return 1;
		}
	}

	switch(message->qos){
		case 0:
			if(mosq->on_message){
				mosq->on_message(mosq->obj, message);
			}else{
				mosquitto_message_cleanup(&message);
			}
			break;
		case 1:
			mosquitto_message_cleanup(&message); // FIXME - temporary!
			//FIXME if(mqtt3_db_messages_queue(mosq->id, topic, qos, retain, store_id)) rc = 1;
			//FIXME if(mqtt3_raw_puback(mosq, mid)) rc = 1;
			break;
		case 2:
			mosquitto_message_cleanup(&message); // FIXME - temporary!
			//FIXME if(mqtt3_db_message_insert(mosq->id, mid, md_in, ms_wait_pubrec, qos, store_id)) rc = 1;
			//FIXME if(mqtt3_raw_pubrec(mosq, mid)) rc = 1;
			break;
	}

	return 0;
}

