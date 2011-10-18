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

#include <string.h>

#include <config.h>

#include <mqtt3.h>
#include <mqtt3_protocol.h>
#include <send_mosq.h>

int mqtt3_raw_puback(struct mosquitto *context, uint16_t mid)
{
	if(context) mqtt3_log_printf(MOSQ_LOG_DEBUG, "Sending PUBACK to %s (Mid: %d)", context->core.id, mid);
	return _mosquitto_send_command_with_mid(&context->core, PUBACK, mid, false);
}

int mqtt3_raw_publish(struct mosquitto *context, int dup, uint8_t qos, uint16_t mid, const char *topic, uint32_t payloadlen, const uint8_t *payload, bool retain)
{
	int len;

	if(!context || context->core.sock == -1 || !topic) return MOSQ_ERR_INVAL;

	if(context->listener && context->listener->mount_point){
		len = strlen(context->listener->mount_point);
		if(len > strlen(topic)){
			topic += strlen(context->listener->mount_point);
		}else{
			/* Invalid topic string. Should never happen, but silently swallow the message anyway. */
			return MOSQ_ERR_SUCCESS;
		}
	}
	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Sending PUBLISH to %s (d%d, q%d, r%d, m%d, '%s', ... (%ld bytes))", context->core.id, dup, qos, retain, mid, topic, (long)payloadlen);
	return _mosquitto_send_real_publish(&context->core, mid, topic, payloadlen, payload, qos, retain, dup);
}

int mqtt3_raw_pubcomp(struct mosquitto *context, uint16_t mid)
{
	if(context) mqtt3_log_printf(MOSQ_LOG_DEBUG, "Sending PUBCOMP to %s (Mid: %d)", context->core.id, mid);
	return _mosquitto_send_command_with_mid(&context->core, PUBCOMP, mid, false);
}

int mqtt3_raw_pubrec(struct mosquitto *context, uint16_t mid)
{
	if(context) mqtt3_log_printf(MOSQ_LOG_DEBUG, "Sending PUBREC to %s (Mid: %d)", context->core.id, mid);
	return _mosquitto_send_command_with_mid(&context->core, PUBREC, mid, false);
}

int mqtt3_raw_pubrel(struct mosquitto *context, uint16_t mid, bool dup)
{
	if(context) mqtt3_log_printf(MOSQ_LOG_DEBUG, "Sending PUBREL to %s (Mid: %d)", context->core.id, mid);
	return _mosquitto_send_command_with_mid(&context->core, PUBREL|2, mid, dup);
}

int mqtt3_raw_pingreq(struct mosquitto *context)
{
	if(context) mqtt3_log_printf(MOSQ_LOG_DEBUG, "Sending PINGREQ to %s", context->core.id);
	return _mosquitto_send_simple_command(&context->core, PINGREQ);
}

int mqtt3_raw_pingresp(struct mosquitto *context)
{
	if(context) mqtt3_log_printf(MOSQ_LOG_DEBUG, "Sending PINGRESP to %s", context->core.id);
	return _mosquitto_send_simple_command(&context->core, PINGRESP);
}

