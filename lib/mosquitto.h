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

#ifndef _MOSQUITTO_H_
#define _MOSQUITTO_H_
#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct _mosquitto_packet{
	uint8_t command;
	uint8_t command_saved;
	uint8_t have_remaining;
	uint8_t remaining_count;
	uint32_t remaining_mult;
	uint32_t remaining_length;
	uint32_t to_process;
	uint32_t pos;
	uint8_t *payload;
	struct _mosquitto_packet *next;
};

struct mosquitto {
	void *obj;
	int sock;
	sqlite3 *db;
	char *id;
	int keepalive;
	bool will;
	char *will_topic;
	uint32_t will_payloadlen;
	uint8_t *will_payload;
	int will_qos;
	bool will_retain;
	struct _mosquitto_packet in_packet;
	void (*on_connect)(void *obj, int rc);
	void (*on_publish)(void *obj, int mid);
	void (*on_message)(void *obj, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain);
	void (*on_subscribe)(void *obj, int mid);
	void (*on_unsubscribe)(void *obj, int mid);
	//void (*on_error)();
};

int mosquitto_lib_init(void);
void mosquitto_lib_cleanup(void);

struct mosquitto *mosquitto_new(void *obj, const char *id);
int mosquitto_will_set(struct mosquitto *mosq, bool will, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain);
void mosquitto_destroy(struct mosquitto *mosq);
int mosquitto_connect(struct mosquitto *mosq, const char *host, int port, int keepalive, bool clean_session);
int mosquitto_disconnect(struct mosquitto *mosq);
int mosquitto_publish(struct mosquitto *mosq, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain);
int mosquitto_subscribe(struct mosquitto *mosq, const char *sub, int qos);
int mosquitto_unsubscribe(struct mosquitto *mosq, const char *sub);
int mosquitto_loop(struct mosquitto *mosq);
int mosquitto_read(struct mosquitto *mosq);
int mosquitto_write(struct mosquitto *mosq);

#endif
