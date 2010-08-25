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

int mqtt3_raw_connect(mqtt3_context *context, const char *client_id, bool will, uint8_t will_qos, bool will_retain, const char *will_topic, const char *will_msg, uint16_t keepalive, bool clean_session)
{
	struct _mosquitto_packet *packet = NULL;
	int payloadlen;

	if(!context || !client_id) return 1;

	packet = _mosquitto_calloc(1, sizeof(struct _mosquitto_packet));
	if(!packet) return 1;

	payloadlen = 2+strlen(client_id);
	if(will && will_topic && will_msg){
		payloadlen += 2+strlen(will_topic) + 2+strlen(will_msg);
	}else{
		will = 0;
	}

	packet->command = CONNECT;
	packet->remaining_length = 12+payloadlen;
	packet->payload = _mosquitto_malloc(sizeof(uint8_t)*(12+payloadlen));
	if(!packet->payload){
		_mosquitto_free(packet);
		return 1;
	}

	/* Variable header */
	if(_mosquitto_write_string(packet, PROTOCOL_NAME, strlen(PROTOCOL_NAME))) return 1;
	if(_mosquitto_write_byte(packet, PROTOCOL_VERSION)) return 1;
	if(_mosquitto_write_byte(packet, ((will_retain&0x1)<<5) | ((will_qos&0x3)<<3) | ((will&0x1)<<2) | ((clean_session&0x1)<<1))) return 1;
	if(_mosquitto_write_uint16(packet, keepalive)) return 1;

	/* Payload */
	if(_mosquitto_write_string(packet, client_id, strlen(client_id))) return 1;
	if(will){
		if(_mosquitto_write_string(packet, will_topic, strlen(will_topic))) return 1;
		if(_mosquitto_write_string(packet, will_msg, strlen(will_msg))) return 1;
	}

	context->keepalive = keepalive;
	if(mqtt3_net_packet_queue(context, packet)) return 1;
	return 0;
}

int mqtt3_raw_disconnect(mqtt3_context *context)
{
	return mqtt3_send_simple_command(context, DISCONNECT);
}

int mqtt3_raw_subscribe(mqtt3_context *context, bool dup, const char *topic, uint8_t topic_qos)
{
	/* FIXME - only deals with a single topic */
	struct _mosquitto_packet *packet = NULL;
	uint32_t packetlen;
	uint16_t mid;

	if(!context || !topic) return 1;

	packet = _mosquitto_calloc(1, sizeof(struct _mosquitto_packet));
	if(!packet) return 1;

	packetlen = 2 + 2+strlen(topic) + 1;

	packet->command = SUBSCRIBE | (dup<<3) | (1<<1);
	packet->remaining_length = packetlen;
	packet->payload = _mosquitto_malloc(sizeof(uint8_t)*packetlen);
	if(!packet->payload){
		_mosquitto_free(packet);
		return 1;
	}

	/* Variable header */
	mid = mqtt3_db_mid_generate(context->id);
	if(_mosquitto_write_uint16(packet, mid)) return 1;

	/* Payload */
	if(_mosquitto_write_string(packet, topic, strlen(topic))) return 1;
	if(_mosquitto_write_byte(packet, topic_qos)) return 1;

	if(mqtt3_net_packet_queue(context, packet)) return 1;
	return 0;
}


int mqtt3_raw_unsubscribe(mqtt3_context *context, bool dup, const char *topic)
{
	/* FIXME - only deals with a single topic */
	struct _mosquitto_packet *packet = NULL;
	uint32_t packetlen;
	uint16_t mid;

	if(!context || !topic) return 1;

	packet = _mosquitto_calloc(1, sizeof(struct _mosquitto_packet));
	if(!packet) return 1;

	packetlen = 2 + 2+strlen(topic);

	packet->command = SUBSCRIBE | (dup<<3) | (1<<1);
	packet->remaining_length = packetlen;
	packet->payload = _mosquitto_malloc(sizeof(uint8_t)*packetlen);
	if(!packet->payload){
		_mosquitto_free(packet);
		return 1;
	}

	/* Variable header */
	mid = mqtt3_db_mid_generate(context->id);
	if(_mosquitto_write_uint16(packet, mid)) return 1;

	/* Payload */
	if(_mosquitto_write_string(packet, topic, strlen(topic))) return 1;

	if(mqtt3_net_packet_queue(context, packet)) return 1;
	return 0;
}

