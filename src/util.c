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

#include <config.h>

#include <assert.h>
#include <string.h>

#include <mqtt3.h>
#include <mqtt3_protocol.h>

/* Convert mqtt command (as defined in mqtt3.h) to corresponding string. */
const char *mqtt3_command_to_string(uint8_t command)
{
	switch(command){
		case CONNACK:
			return "CONNACK";
		case CONNECT:
			return "CONNECT";
		case DISCONNECT:
			return "DISCONNECT";
		case PINGREQ:
			return "PINGREQ";
		case PINGRESP:
			return "PINGRESP";
		case PUBACK:
			return "PUBACK";
		case PUBCOMP:
			return "PUBCOMP";
		case PUBLISH:
			return "PUBLISH";
		case PUBREC:
			return "PUBREC";
		case PUBREL:
			return "PUBREL";
		case SUBACK:
			return "SUBACK";
		case SUBSCRIBE:
			return "SUBSCRIBE";
		case UNSUBACK:
			return "UNSUBACK";
		case UNSUBSCRIBE:
			return "UNSUBSCRIBE";
	}
	return "UNKNOWN";
}

void mqtt3_check_keepalive(mqtt3_context *context)
{
	if(context && context->core.sock != -1 && time(NULL) - context->core.last_msg_out >= context->core.keepalive){
		if(context->core.state == mosq_cs_connected){
			mqtt3_raw_pingreq(context);
		}else{
			if(context->listener){
				context->listener->client_count--;
				assert(context->listener->client_count >= 0);
			}
			context->listener = NULL;
			_mosquitto_socket_close(&context->core);
		}
	}
}

