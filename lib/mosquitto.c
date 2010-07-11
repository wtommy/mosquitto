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
#include <database_mosq.h>
#include <net_mosq.h>

#include <errno.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mosquitto_lib_init(void)
{
	if(sqlite3_initialize() != SQLITE_OK){
		return 1;
	}

	return 0;
}

void mosquitto_lib_cleanup(void)
{
	sqlite3_shutdown();
}

struct mosquitto *mosquitto_new(void *obj, const char *id)
{
	struct mosquitto *mosq = NULL;

	if(!id) return NULL;

	mosq = (struct mosquitto *)calloc(1, sizeof(struct mosquitto));
	if(mosq){
		mosq->obj = obj;
		mosq->sock = -1;
		mosq->db = NULL;
		mosq->keepalive = 60;
		mosq->id = strdup(id);
		mosq->will = false;
		mosq->will_topic = NULL;
		mosq->will_payloadlen = 0;
		mosq->will_payload = NULL;
		mosq->will_qos = 0;
		mosq->will_retain = false;
		mosq->on_connect = NULL;
		mosq->on_publish = NULL;
		mosq->on_message = NULL;
		mosq->on_subscribe = NULL;
		mosq->on_unsubscribe = NULL;
	}
	return mosq;
}

int mosquitto_will_set(struct mosquitto *mosq, bool will, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain)
{
	if(!mosq) return 1;

	if(mosq->will_topic) free(mosq->will_topic);
	if(mosq->will_payload){
		free(mosq->will_payload);
		mosq->will_payload = NULL;
	}

	mosq->will = will;
	mosq->will_topic = strdup(topic);
	mosq->will_payloadlen = payloadlen;
	if(mosq->will_payloadlen > 0){
		if(!payload) return 1;
		mosq->will_payload = malloc(sizeof(uint8_t)*mosq->will_payloadlen);
		if(!mosq->will_payload) return 1;

		memcpy(mosq->will_payload, payload, payloadlen);
	}
	mosq->will_qos = qos;
	mosq->will_retain = retain;

	return 0;
}

void mosquitto_destroy(struct mosquitto *mosq)
{
	if(mosq->id) free(mosq->id);
	if(mosq->db) _mosquitto_db_close(mosq->db);

	free(mosq);
}

int mosquitto_connect(struct mosquitto *mosq, const char *host, int port, int keepalive, bool clean_session)
{
	if(!mosq || !host || !port) return 1;

	mosq->sock = _mosquitto_socket_connect(host, port);
/* FIXME
	*context = mqtt3_context_init(sock);
	if((*context)->sock == -1){
		return 1;
	}

	(*context)->id = mqtt3_strdup(id);
	mqtt3_raw_connect(*context, id,
			mosq->will_topic, mosq->will_payload, mosq->will_qos, mosq->will_retain
			keepalive, clean_session);
*/
	return 0;
}

int mosquitto_disconnect(struct mosquitto *mosq)
{
	return 0;
}

int mosquitto_publish(struct mosquitto *mosq, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain)
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
	struct timespec timeout;
	fd_set readfds, writefds;
	int fdcount;

	if(!mosq || mosq->sock < 0) return 1;

	FD_ZERO(&readfds);
	FD_SET(mosq->sock, &readfds);
	FD_ZERO(&writefds);
	/* FIXME
	if(mosq->out_packet){
	*/
		FD_SET(mosq->sock, &writefds);
	/* FIXME
	}
	*/
	timeout.tv_sec = 1;
	timeout.tv_nsec = 0;

	fdcount = pselect(mosq->sock+1, &readfds, &writefds, NULL, &timeout, NULL);
	if(fdcount == -1){
		fprintf(stderr, "Error in pselect: %s\n", strerror(errno));
		return 1;
	}else{
		if(FD_ISSET(mosq->sock, &readfds)){
			if(mosquitto_read(mosq)){
				/* FIXME
				mosquitto_socket_close(mosq);
				*/
				return 1;
			}
		}
		if(FD_ISSET(mosq->sock, &writefds)){
			if(mosquitto_write(mosq)){
				/* FIXME
				mosquitto_socket_close(mosq);
				*/
				return 1;
			}
		}
	}
	/* FIXME
	mosquitto_check_keepalive(mosq);
	*/

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

