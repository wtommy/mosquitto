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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>

#include <config.h>
#include <mqtt3.h>

int mqtt3_packet_handle(mqtt3_context *context)
{
	if(!context) return 1;

	switch((context->in_packet.command)&0xF0){
		case PINGREQ:
			return mqtt3_handle_pingreq(context);
		case PINGRESP:
			return mqtt3_handle_pingresp(context);
		case PUBACK:
			return mqtt3_handle_puback(context);
		case PUBCOMP:
			return mqtt3_handle_pubcomp(context);
		case PUBLISH:
			return mqtt3_handle_publish(context);
		case PUBREC:
			return mqtt3_handle_pubrec(context);
		case PUBREL:
			return mqtt3_handle_pubrel(context);
#ifdef WITH_BROKER
		case CONNECT:
			return mqtt3_handle_connect(context);
		case DISCONNECT:
			return mqtt3_handle_disconnect(context);
		case SUBSCRIBE:
			return mqtt3_handle_subscribe(context);
		case UNSUBSCRIBE:
			return mqtt3_handle_unsubscribe(context);
#endif
#ifdef WITH_CLIENT
		case CONNACK:
			return mqtt3_handle_connack(context);
		case SUBACK:
			return mqtt3_handle_suback(context);
		case UNSUBACK:
			return mqtt3_handle_unsuback(context);
#endif
		default:
			/* If we don't recognise the command, return an error straight away. */
			return 1;
	}
}

int mqtt3_handle_puback(mqtt3_context *context)
{
	uint16_t mid;

	if(mqtt3_read_uint16(context, &mid)) return 1;
	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received PUBACK from %s (Mid: %d)", context->id, mid);

	if(mid){
		if(mqtt3_db_message_delete(context->id, mid, md_out)) return 1;
	}
	return 0;
}

int mqtt3_handle_pingreq(mqtt3_context *context)
{
	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received PINGREQ from %s", context->id);
	return mqtt3_raw_pingresp(context);
}

int mqtt3_handle_pingresp(mqtt3_context *context)
{
	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received PINGRESP from %s", context->id);
	return 0;
}

int mqtt3_handle_pubcomp(mqtt3_context *context)
{
	uint16_t mid;

	if(mqtt3_read_uint16(context, &mid)) return 1;
	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received PUBCOMP from %s (Mid: %d)", context->id, mid);

	if(mid){
		if(mqtt3_db_message_delete(context->id, mid, md_out)) return 1;
	}
	return 0;
}

int mqtt3_handle_publish(mqtt3_context *context)
{
	char *topic;
	uint8_t *payload;
	uint32_t payloadlen;
	uint8_t dup, qos, retain;
	uint16_t mid;
	int rc = 0;
	uint8_t header = context->in_packet.command;

	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received PUBLISH from %s", context->id);
	dup = (header & 0x08)>>3;
	qos = (header & 0x06)>>1;
	retain = (header & 0x01);

	if(mqtt3_read_string(context, &topic)) return 1;

	if(qos > 0){
		if(mqtt3_read_uint16(context, &mid)){
			mqtt3_free(topic);
			return 1;
		}
	}

	payloadlen = context->in_packet.remaining_length - context->in_packet.pos;
	payload = mqtt3_calloc(payloadlen+1, sizeof(uint8_t));
	if(mqtt3_read_bytes(context, payload, payloadlen)){
		mqtt3_free(topic);
		return 1;
	}
#ifdef DEBUG
	printf("%s: %s\n", topic, payload);
#endif

	switch(qos){
		case 0:
			if(mqtt3_db_messages_queue(topic, qos, payloadlen, payload, retain)) rc = 1;
			break;
		case 1:
			if(mqtt3_db_messages_queue(topic, qos, payloadlen, payload, retain)) rc = 1;
			if(mqtt3_raw_puback(context, mid)) rc = 1;
			break;
		case 2:
			if(mqtt3_db_message_insert(context->id, mid, md_in, ms_wait_pubrec, retain, topic, qos, payloadlen, payload)) rc = 1;
			if(mqtt3_raw_pubrec(context, mid)) rc = 1;
			break;
	}
	mqtt3_free(topic);
	mqtt3_free(payload);

	return rc;
}

int mqtt3_handle_pubrec(mqtt3_context *context)
{
	uint16_t mid;

	if(mqtt3_read_uint16(context, &mid)) return 1;
	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received PUBREC from %s (Mid: %d)", context->id, mid);

	if(mqtt3_db_message_update(context->id, mid, md_out, ms_wait_pubcomp)) return 1;
	if(mqtt3_raw_pubrel(context, mid)) return 1;

	return 0;
}

int mqtt3_handle_pubrel(mqtt3_context *context)
{
	uint16_t mid;

	if(mqtt3_read_uint16(context, &mid)) return 1;
	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received PUBREL from %s (Mid: %d)", context->id, mid);

	if(mqtt3_db_message_release(context->id, mid, md_in)) return 1;
	if(mqtt3_raw_pubcomp(context, mid)) return 1;

	return 0;
}

