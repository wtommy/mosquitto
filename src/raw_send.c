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

#include <stdint.h>
#include <string.h>

#include <mqtt3.h>

int mqtt3_raw_puback(mqtt3_context *context, uint16_t mid)
{
	if(context) mqtt3_log_printf(MQTT3_LOG_DEBUG, "Sending PUBACK to %d (%d)", context->sock, mid);
	return mqtt3_send_command_with_mid(context, PUBACK, mid);
}

int mqtt3_raw_publish(mqtt3_context *context, bool dup, uint8_t qos, bool retain, uint16_t mid, const char *sub, uint32_t payloadlen, const uint8_t *payload)
{
	int packetlen;

	if(!context || context->sock == -1 || !sub || !payload) return 1;

	if(context) mqtt3_log_printf(MQTT3_LOG_DEBUG, "Sending PUBLISH to %d (%d, %d, %d, %d, '%s', ...)", context->sock, dup, qos, retain, mid, sub);

	packetlen = 2+strlen(sub) + payloadlen;
	if(qos > 0) packetlen += 2; /* For message id */

	/* Fixed header */
	if(mqtt3_write_byte(context, PUBLISH | (dup<<3) | (qos<<1) | retain)) return 1;
	if(mqtt3_write_remaining_length(context, packetlen)) return 1;

	/* Variable header (topic string) */
	if(mqtt3_write_string(context, sub, strlen(sub))) return 1;
	if(qos > 0){
		if(mqtt3_write_uint16(context, mid)) return 1;
	}

	/* Payload */
	if(mqtt3_write_bytes(context, payload, payloadlen)) return 1;

	context->last_msg_out = time(NULL);
	return 0;
}

int mqtt3_raw_pubcomp(mqtt3_context *context, uint16_t mid)
{
	if(context) mqtt3_log_printf(MQTT3_LOG_DEBUG, "Sending PUBCOMP to %d (%d)", context->sock, mid);
	return mqtt3_send_command_with_mid(context, PUBCOMP, mid);
}

int mqtt3_raw_pubrec(mqtt3_context *context, uint16_t mid)
{
	if(context) mqtt3_log_printf(MQTT3_LOG_DEBUG, "Sending PUBREC to %d (%d)", context->sock, mid);
	return mqtt3_send_command_with_mid(context, PUBREC, mid);
}

int mqtt3_raw_pubrel(mqtt3_context *context, uint16_t mid)
{
	if(context) mqtt3_log_printf(MQTT3_LOG_DEBUG, "Sending PUBREL to %d (%d)", context->sock, mid);
	return mqtt3_send_command_with_mid(context, PUBREL, mid);
}

/* For PUBACK, PUBCOMP, PUBREC, and PUBREL */
int mqtt3_send_command_with_mid(mqtt3_context *context, uint8_t command, uint16_t mid)
{
	if(mqtt3_write_byte(context, command)) return 1;
	if(mqtt3_write_remaining_length(context, 2)) return 1;
	if(mqtt3_write_uint16(context, mid)) return 1;

	context->last_msg_out = time(NULL);
	return 0;
}

/* For DISCONNECT, PINGREQ and PINGRESP */
int mqtt3_send_simple_command(mqtt3_context *context, uint8_t command)
{
	if(mqtt3_write_byte(context, command)) return 1;
	if(mqtt3_write_byte(context, 0)) return 1;

	context->last_msg_out = time(NULL);
	return 0;
}

int mqtt3_raw_pingreq(mqtt3_context *context)
{
	if(context) mqtt3_log_printf(MQTT3_LOG_DEBUG, "Sending PINGREQ to %d", context->sock);
	return mqtt3_send_simple_command(context, PINGREQ);
}

int mqtt3_raw_pingresp(mqtt3_context *context)
{
	if(context) mqtt3_log_printf(MQTT3_LOG_DEBUG, "Sending PINGRESP to %d", context->sock);
	return mqtt3_send_simple_command(context, PINGRESP);
}

