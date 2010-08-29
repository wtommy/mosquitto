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

#include <mosquitto.h>
#include <logging_mosq.h>
#include <memory_mosq.h>
#include <net_mosq.h>
#include <read_handle.h>

int _mosquitto_handle_connack(struct mosquitto *mosq)
{
	uint8_t byte;
	uint8_t rc;

	assert(mosq);
	if(mosq->core.in_packet.remaining_length != 2){
		return MOSQ_ERR_PROTOCOL;
	}
	_mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Received CONNACK");
	if(_mosquitto_read_byte(&mosq->core.in_packet, &byte)) return 1; // Reserved byte, not used
	if(_mosquitto_read_byte(&mosq->core.in_packet, &rc)) return 1;
	if(mosq->on_connect){
		mosq->on_connect(mosq->obj, rc);
	}
	switch(rc){
		case 0:
			mosq->core.state = mosq_cs_connected;
			return MOSQ_ERR_SUCCESS;
		case 1:
			_mosquitto_log_printf(mosq, MOSQ_LOG_ERR, "Connection Refused: unacceptable protocol version");
			return 1;
		case 2:
			_mosquitto_log_printf(mosq, MOSQ_LOG_ERR, "Connection Refused: identifier rejected");
			return 1;
		case 3:
			_mosquitto_log_printf(mosq, MOSQ_LOG_ERR, "Connection Refused: broker unavailable");
			return 1;
	}
	return 1;
}

int _mosquitto_handle_suback(struct mosquitto *mosq)
{
	uint16_t mid;
	uint8_t *granted_qos;
	int qos_count;
	int i = 0;

	assert(mosq);
	_mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Received SUBACK");
	if(_mosquitto_read_uint16(&mosq->core.in_packet, &mid)) return 1;

	qos_count = mosq->core.in_packet.remaining_length - mosq->core.in_packet.pos;
	granted_qos = _mosquitto_malloc(qos_count*sizeof(uint8_t));
	if(!granted_qos) return MOSQ_ERR_NOMEM;
	while(mosq->core.in_packet.pos < mosq->core.in_packet.remaining_length){
		if(_mosquitto_read_byte(&mosq->core.in_packet, &(granted_qos[i]))){
			_mosquitto_free(granted_qos);
			return 1;
		}
		i++;
	}
	if(mosq->on_subscribe){
		mosq->on_subscribe(mosq->obj, mid, qos_count, granted_qos);
	}
	_mosquitto_free(granted_qos);

	return MOSQ_ERR_SUCCESS;
}

int _mosquitto_handle_unsuback(struct mosquitto *mosq)
{
	uint16_t mid;

	assert(mosq);
	if(mosq->core.in_packet.remaining_length != 2){
		return MOSQ_ERR_PROTOCOL;
	}
	_mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Received UNSUBACK");
	if(_mosquitto_read_uint16(&mosq->core.in_packet, &mid)) return 1;
	if(mosq->on_unsubscribe) mosq->on_unsubscribe(mosq->obj, mid);

	return MOSQ_ERR_SUCCESS;
}
