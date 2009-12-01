/*
Copyright (c) 2009, Roger Light <roger@atchoo.org>
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

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <mqtt3.h>

int mqtt3_raw_connect(mqtt3_context *context, const char *client_id, int client_id_len, bool will, uint8_t will_qos, bool will_retain, const char *will_topic, int will_topic_len, const char *will_msg, int will_msg_len, uint16_t keepalive, bool clean_start)
{
	int payloadlen;

	payloadlen = 2+client_id_len;
	if(will) payloadlen += 2+will_topic_len + 2+will_msg_len;

	/* Fixed header */
	if(mqtt3_write_byte(context, CONNECT)) return 1;
	if(mqtt3_write_remaining_length(context, 12+payloadlen)) return 1;

	/* Variable header */
	if(mqtt3_write_string(context, PROTOCOL_NAME, strlen(PROTOCOL_NAME))) return 1;
	if(mqtt3_write_byte(context, PROTOCOL_VERSION)) return 1;
	if(mqtt3_write_byte(context, (will_retain<<5) | (will_qos<<3) | (will<<2) | (clean_start<<1))) return 1;
	if(mqtt3_write_uint16(context, keepalive)) return 1;

	/* Payload */
	if(mqtt3_write_string(context, client_id, client_id_len)) return 1;
	if(will){
		if(mqtt3_write_string(context, will_topic, will_topic_len)) return 1;
		if(mqtt3_write_string(context, will_msg, will_msg_len)) return 1;
	}

	context->last_msg_out = time(NULL);
	context->keepalive = keepalive;
	return 0;
}

int mqtt3_raw_disconnect(mqtt3_context *context)
{
	return mqtt3_send_simple_command(context, DISCONNECT);
}

int mqtt3_raw_subscribe(mqtt3_context *context, bool dup, const char *topic, uint16_t topiclen, uint8_t topic_qos)
{
	/* FIXME - only deals with a single topic */
	uint32_t packetlen;
	uint16_t mid;

	packetlen = 2 + 2+topiclen + 1;

	/* Fixed header */
	if(mqtt3_write_byte(context, SUBSCRIBE | (dup<<3) | (1<<1))) return 1;
	if(mqtt3_write_remaining_length(context, packetlen)) return 1;

	/* Variable header */
	mid = mqtt3_db_mid_generate(context->id);
	if(mqtt3_write_uint16(context, mid)) return 1;

	/* Payload */
	if(mqtt3_write_string(context, topic, topiclen)) return 1;
	if(mqtt3_write_byte(context, topic_qos)) return 1;

	context->last_msg_out = time(NULL);
	return 0;
}


int mqtt3_raw_unsubscribe(mqtt3_context *context, bool dup, const char *topic, uint16_t topiclen)
{
	/* FIXME - only deals with a single topic */
	uint32_t packetlen;
	uint16_t mid;

	packetlen = 2 + 2+topiclen;

	/* Fixed header */
	if(mqtt3_write_byte(context, UNSUBSCRIBE | (dup<<3) | (1<<1))) return 1;
	if(mqtt3_write_remaining_length(context, packetlen)) return 1;
	
	/* Variable header */
	mid = mqtt3_db_mid_generate(context->id);
	if(mqtt3_write_uint16(context, mid)) return 1;

	/* Payload */
	if(mqtt3_write_string(context, topic, topiclen)) return 1;

	context->last_msg_out = time(NULL);
	return 0;
}

