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

#include <mqtt3.h>

int mqtt3_handle_puback(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received PUBACK");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;

	if(mid){
		if(mqtt3_db_message_delete(context->id, mid, md_out)) return 1;
	}
	return 0;
}

int mqtt3_handle_pingreq(mqtt3_context *context)
{
	uint32_t remaining_length;
	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received PINGREQ");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	return mqtt3_raw_pingresp(context);
}

int mqtt3_handle_pingresp(mqtt3_context *context)
{
	uint32_t remaining_length;
	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received PINGRESP");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	return 0;
}

int mqtt3_handle_pubcomp(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received PUBCOMP");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;

	if(mid){
		if(mqtt3_db_message_delete(context->id, mid, md_out)) return 1;
	}
	return 0;
}

int mqtt3_handle_publish(mqtt3_context *context, uint8_t header)
{
	char *sub;
	uint8_t *payload;
	uint32_t remaining_length;
	uint32_t payloadlen;
	uint8_t dup, qos, retain;
	uint16_t mid;
	int rc = 0;

	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received PUBLISH");
	dup = (header & 0x08)>>3;
	qos = (header & 0x06)>>1;
	retain = (header & 0x01);

	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;

	if(mqtt3_read_string(context, &sub)) return 1;
	remaining_length -= strlen((char *)sub) + 2;

	if(qos > 0){
		if(mqtt3_read_uint16(context, &mid)){
			mqtt3_free(sub);
			return 1;
		}
		remaining_length -= 2;
	}

	payloadlen = remaining_length;
	payload = mqtt3_calloc(payloadlen+1, sizeof(uint8_t));
	if(mqtt3_read_bytes(context, payload, payloadlen)){
		mqtt3_free(sub);
		return 1;
	}
#ifdef DEBUG
	printf("%s: %s\n", sub, payload);
#endif

	switch(qos){
		case 0:
			if(mqtt3_db_messages_queue(sub, qos, payloadlen, payload, retain)) rc = 1;
			break;
		case 1:
			if(mqtt3_db_messages_queue(sub, qos, payloadlen, payload, retain)) rc = 1;
			if(mqtt3_raw_puback(context, mid)) rc = 1;
			break;
		case 2:
			if(mqtt3_db_message_insert(context->id, mid, md_in, ms_wait_pubrec, retain, sub, qos, payloadlen, payload)) rc = 1;
			if(mqtt3_raw_pubrec(context, mid)) rc = 1;
			break;
	}
	mqtt3_free(sub);
	mqtt3_free(payload);

	return rc;
}

int mqtt3_handle_pubrec(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received PUBREC");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;

	if(mqtt3_db_message_update(context->id, mid, md_out, ms_wait_pubcomp)) return 1;
	if(mqtt3_raw_pubrel(context, mid)) return 1;

	return 0;
}

int mqtt3_handle_pubrel(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received PUBREL");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;

	if(mqtt3_db_message_release(context->id, mid, md_in)) return 1;
	if(mqtt3_raw_pubcomp(context, mid)) return 1;

	return 0;
}

