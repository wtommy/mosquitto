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

#include <assert.h>
#include <string.h>

#include <mosquitto.h>
#include <memory_mosq.h>
#include <mqtt3_protocol.h>
#include <net_mosq.h>
#include <send_mosq.h>
#include <util_mosq.h>

int _mosquitto_send_connect(struct mosquitto *mosq, uint16_t keepalive, bool clean_session)
{
	struct _mosquitto_packet *packet = NULL;
	int payloadlen;
	uint8_t will = 0;
	uint8_t byte;

	assert(mosq);
	assert(mosq->core.id);

	packet = _mosquitto_calloc(1, sizeof(struct _mosquitto_packet));
	if(!packet) return 1;

	payloadlen = 2+strlen(mosq->core.id);
	if(mosq->will){
		will = 1;
		if(mosq->will->topic){
			payloadlen += 2+strlen(mosq->will->topic) + 2+mosq->will->payloadlen;
		}else{
			//FIXME - error
			return 1;
		}
	}
	if(mosq->core.username){
		payloadlen += 2+strlen(mosq->core.username);
		if(mosq->core.password){
			payloadlen += 2+strlen(mosq->core.password);
		}
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
	byte = (clean_session&0x1)<<1;
	if(will){
		byte = byte | ((mosq->will->retain&0x1)<<5) | ((mosq->will->qos&0x3)<<3) | ((will&0x1)<<2);
	}
	if(mosq->core.username){
		byte = byte | 0x1<<6;
		if(mosq->core.password){
			byte = byte | 0x1<<7;
		}
	}
	if(_mosquitto_write_byte(packet, byte)) return 1;
	if(_mosquitto_write_uint16(packet, keepalive)) return 1;

	/* Payload */
	if(_mosquitto_write_string(packet, mosq->core.id, strlen(mosq->core.id))) return 1;
	if(will){
		if(_mosquitto_write_string(packet, mosq->will->topic, strlen(mosq->will->topic))) return 1;
		if(_mosquitto_write_string(packet, (const char *)mosq->will->payload, mosq->will->payloadlen)) return 1;
	}
	if(mosq->core.username){
		if(_mosquitto_write_string(packet, mosq->core.username, strlen(mosq->core.username))) return 1;
		if(mosq->core.password){
			if(_mosquitto_write_string(packet, mosq->core.password, strlen(mosq->core.password))) return 1;
		}
	}

	mosq->core.keepalive = keepalive;
	if(_mosquitto_packet_queue(mosq, packet)) return 1;
	return 0;
}

int _mosquitto_send_disconnect(struct mosquitto *mosq)
{
	assert(mosq);
	return _mosquitto_send_simple_command(mosq, DISCONNECT);
}

int _mosquitto_send_subscribe(struct mosquitto *mosq, uint16_t *mid, bool dup, const char *topic, uint8_t topic_qos)
{
	/* FIXME - only deals with a single topic */
	struct _mosquitto_packet *packet = NULL;
	uint32_t packetlen;
	uint16_t local_mid;

	assert(mosq);
	assert(topic);

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
	local_mid = _mosquitto_mid_generate(mosq);
	if(mid) *mid = local_mid;
	if(_mosquitto_write_uint16(packet, local_mid)) return 1;

	/* Payload */
	if(_mosquitto_write_string(packet, topic, strlen(topic))) return 1;
	if(_mosquitto_write_byte(packet, topic_qos)) return 1;

	if(_mosquitto_packet_queue(mosq, packet)) return 1;
	return 0;
}


int _mosquitto_send_unsubscribe(struct mosquitto *mosq, uint16_t *mid, bool dup, const char *topic)
{
	/* FIXME - only deals with a single topic */
	struct _mosquitto_packet *packet = NULL;
	uint32_t packetlen;
	uint16_t local_mid;

	assert(mosq);
	assert(topic);

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
	local_mid = _mosquitto_mid_generate(mosq);
	if(mid) *mid = local_mid;
	if(_mosquitto_write_uint16(packet, local_mid)) return 1;

	/* Payload */
	if(_mosquitto_write_string(packet, topic, strlen(topic))) return 1;

	if(_mosquitto_packet_queue(mosq, packet)) return 1;
	return 0;
}

