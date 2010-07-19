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
#include <string.h>

#include <mosquitto.h>
#include <mqtt3_protocol.h>
#include <net_mosq.h>

/* For DISCONNECT, PINGREQ and PINGRESP */
int _mosquitto_send_simple_command(struct mosquitto *mosq, uint8_t command)
{
	struct _mosquitto_packet *packet = NULL;

	packet = calloc(1, sizeof(struct _mosquitto_packet));
	if(!packet) return 1;

	packet->command = command;
	packet->remaining_length = 0;

	if(_mosquitto_packet_queue(mosq, packet)){
		free(packet);
		return 1;
	}

	return 0;
}

int _mosquitto_raw_pingreq(struct mosquitto *mosq)
{
	// FIXME if(mosq) _mosquitto_log_printf(MQTT3_LOG_DEBUG, "Sending PINGREQ to %s", mosq->id);
	return _mosquitto_send_simple_command(mosq, PINGREQ);
}

int _mosquitto_raw_pingresp(struct mosquitto *mosq)
{
	// FIXME if(mosq) _mosquitto_log_printf(MQTT3_LOG_DEBUG, "Sending PINGRESP to %s", mosq->id);
	return _mosquitto_send_simple_command(mosq, PINGRESP);
}

