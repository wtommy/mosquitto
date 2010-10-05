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

#include <stdint.h>

#include <config.h>
#include <mqtt3.h>
#include <mqtt3_protocol.h>
#include <memory_mosq.h>

int mqtt3_raw_connack(mqtt3_context *context, uint8_t result)
{
	struct _mosquitto_packet *packet = NULL;

	if(context) mqtt3_log_printf(MOSQ_LOG_DEBUG, "Sending CONNACK to %s (%d)", context->core.id, result);

	packet = _mosquitto_calloc(1, sizeof(struct _mosquitto_packet));
	if(!packet) return MOSQ_ERR_NOMEM;

	packet->command = CONNACK;
	packet->remaining_length = 2;
	packet->payload = _mosquitto_malloc(sizeof(uint8_t)*2);
	if(!packet->payload){
		_mosquitto_free(packet);
		return MOSQ_ERR_NOMEM;
	}
	packet->payload[0] = 0;
	packet->payload[1] = result;

	if(mqtt3_net_packet_queue(context, packet)) return 1;
	return MOSQ_ERR_SUCCESS;
}

int mqtt3_raw_suback(mqtt3_context *context, uint16_t mid, uint32_t payloadlen, const uint8_t *payload)
{
	struct _mosquitto_packet *packet = NULL;

	mqtt3_log_printf(MOSQ_LOG_DEBUG, "Sending SUBACK to %s", context->core.id);

	packet = _mosquitto_calloc(1, sizeof(struct _mosquitto_packet));
	if(!packet) return MOSQ_ERR_NOMEM;

	packet->command = SUBACK;
	packet->remaining_length = 2+payloadlen;
	packet->payload = _mosquitto_malloc(sizeof(uint8_t)*(2+payloadlen));
	if(!packet->payload){
		_mosquitto_free(packet);
		return MOSQ_ERR_NOMEM;
	}
	_mosquitto_write_uint16(packet, mid);
	if(payloadlen){
		_mosquitto_write_bytes(packet, payload, payloadlen);
	}

	if(mqtt3_net_packet_queue(context, packet)) return 1;

	return MOSQ_ERR_SUCCESS;
}
