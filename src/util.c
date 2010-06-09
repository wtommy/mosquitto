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

#include <config.h>
#include <stdint.h>
#include <string.h>

#include <mqtt3.h>

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
	if(context && context->sock != -1 && time(NULL) - context->last_msg_out >= context->keepalive){
		if(context->connected){
			mqtt3_raw_pingreq(context);
		}else{
			mqtt3_socket_close(context);
		}
	}
}

/* Convert ////some////over/slashed///topic/etc/etc//
 * into some/over/slashed/topic/etc/etc
 */
int mqtt3_fix_sub_topic(char **subtopic)
{
	char *fixed = NULL;
	char *token;

	if(!subtopic || !(*subtopic)) return 1;

	/* size of fixed here is +1 for the terminating 0 and +1 for the spurious /
	 * that gets appended. */
	fixed = mqtt3_calloc(strlen(*subtopic)+2, 1);
	if(!fixed) return 1;

	token = strtok(*subtopic, "/");
	while(token){
		strcat(fixed, token);
		strcat(fixed, "/");
		token = strtok(NULL, "/");
	}

	fixed[strlen(fixed)-1] = '\0';
	mqtt3_free(*subtopic);
	*subtopic = fixed;
	return 0;
}

