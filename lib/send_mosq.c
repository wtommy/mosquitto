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

#include <assert.h>
#include <string.h>

#include <mosquitto.h>
#include <mosquitto_internal.h>
#include <logging_mosq.h>
#include <mqtt3_protocol.h>
#include <net_mosq.h>
#include <send_mosq.h>

int _mosquitto_send_pingreq(struct mosquitto *mosq)
{
	assert(mosq);
	_mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Sending PINGREQ");
	return _mosquitto_send_simple_command(mosq, PINGREQ);
}

int _mosquitto_send_pingresp(struct mosquitto *mosq)
{
	if(mosq) _mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Sending PINGRESP");
	return _mosquitto_send_simple_command(mosq, PINGRESP);
}

int _mosquitto_send_puback(struct mosquitto *mosq, uint16_t mid)
{
	if(mosq) _mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Sending PUBACK (Mid: %d)", mid);
	return _mosquitto_send_command_with_mid(mosq, PUBACK, mid, false);
}

int _mosquitto_send_pubcomp(struct mosquitto *mosq, uint16_t mid)
{
	if(mosq) _mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Sending PUBCOMP (Mid: %d)", mid);
	return _mosquitto_send_command_with_mid(mosq, PUBCOMP, mid, false);
}

int _mosquitto_send_publish(struct mosquitto *mosq, uint16_t mid, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain, bool dup)
{
	assert(mosq);
	assert(topic);

	if(mosq->sock == INVALID_SOCKET) return MOSQ_ERR_NO_CONN;
	if(mosq) _mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Sending PUBLISH (d%d, q%d, r%d, m%d, '%s', ... (%ld bytes))", dup, qos, retain, mid, topic, (long)payloadlen);
	return _mosquitto_send_real_publish(mosq, mid, topic, payloadlen, payload, qos, retain, dup);
}

int _mosquitto_send_pubrec(struct mosquitto *mosq, uint16_t mid)
{
	if(mosq) _mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Sending PUBREC (Mid: %d)", mid);
	return _mosquitto_send_command_with_mid(mosq, PUBREC, mid, false);
}

int _mosquitto_send_pubrel(struct mosquitto *mosq, uint16_t mid, bool dup)
{
	if(mosq) _mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Sending PUBREL (Mid: %d)", mid);
	return _mosquitto_send_command_with_mid(mosq, PUBREL|2, mid, dup);
}

