/*
Copyright (c) 2010, Roger Light <roger@atchoo.org>
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

#include <mosquitto.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void mosquitto_lib_init(void)
{
}

void mosquitto_lib_cleanup(void)
{
}

struct mosquitto *mosquitto_new(void *obj, const char *id)
{
	struct mosquitto *mosq = NULL;

	if(!id) return NULL;

	mosq = (struct mosquitto *)calloc(1, sizeof(struct mosquitto));
	if(mosq){
		mosq->obj = obj;
		mosq->sock = -1;
		mosq->keepalive = 60;
		mosq->id = strdup(id);
		mosq->will = 0;
		mosq->will_topic = NULL;
		mosq->will_payloadlen = 0;
		mosq->will_payload = NULL;
		mosq->will_qos = 0;
		mosq->will_retain = 0;
		mosq->on_connect = NULL;
		mosq->on_publish = NULL;
		mosq->on_message = NULL;
		mosq->on_subscribe = NULL;
		mosq->on_unsubscribe = NULL;
	}
	return mosq;
}

void mosquitto_destroy(struct mosquitto *mosq)
{
	if(mosq->id) free(mosq->id);

	free(mosq);
}

int mosquitto_connect(struct mosquitto *mosq, const char *host, int port)
{
	return 0;
}

int mosquitto_disconnect(struct mosquitto *mosq)
{
	return 0;
}

int mosquitto_publish(struct mosquitto *mosq, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, int retain)
{
	return 0;
}

int mosquitto_subscribe(struct mosquitto *mosq, const char *sub, int qos)
{
	return 0;
}

int mosquitto_unsubscribe(struct mosquitto *mosq, const char *sub)
{
	return 0;
}

int mosquitto_loop(struct mosquitto *mosq)
{
	return 0;
}

int mosquitto_read(struct mosquitto *mosq)
{
	return 0;
}

int mosquitto_write(struct mosquitto *mosq)
{
	return 0;
}

