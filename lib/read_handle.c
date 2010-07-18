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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>

#include <mosquitto.h>
#include <read_handle.h>

int _mosquitto_packet_handle(struct mosquitto *mosq)
{
	if(!mosq) return 1;

	switch((mosq->in_packet.command)&0xF0){
		// FIXME case PINGREQ:
			// FIXME return mqtt3_handle_pingreq(context);
		// FIXME case PINGRESP:
			// FIXME return mqtt3_handle_pingresp(context);
		// FIXME case PUBACK:
			// FIXME return mqtt3_handle_puback(context);
		// FIXME case PUBCOMP:
			// FIXME return mqtt3_handle_pubcomp(context);
		// FIXME case PUBLISH:
			// FIXME return mqtt3_handle_publish(context);
		// FIXME case PUBREC:
			// FIXME return mqtt3_handle_pubrec(context);
		// FIXME case PUBREL:
			// FIXME return mqtt3_handle_pubrel(context);
		case CONNACK:
			return _mosquitto_handle_connack(context);
		case SUBACK:
			return _mosquitto_handle_suback(context);
		case UNSUBACK:
			return _mosquitto_handle_unsuback(context);
		default:
			/* If we don't recognise the command, return an error straight away. */
			return 1;
	}
}

