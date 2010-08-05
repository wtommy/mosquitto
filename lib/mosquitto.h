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

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#ifdef mosquitto_EXPORTS
#define mosq_EXPORT  __declspec(dllexport)
#else
#define mosq_EXPORT  __declspec(dllimport)
#endif
#else
#define mosq_EXPORT
#endif


#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

/* Log destinations */
#define MOSQ_LOG_NONE 0x00
#define MOSQ_LOG_STDOUT 0x04
#define MOSQ_LOG_STDERR 0x08

/* Log types */
#define MOSQ_LOG_INFO 0x01
#define MOSQ_LOG_NOTICE 0x02
#define MOSQ_LOG_WARNING 0x04
#define MOSQ_LOG_ERR 0x08
#define MOSQ_LOG_DEBUG 0x10

enum mosquitto_msg_direction {
	mosq_md_in = 0,
	mosq_md_out = 1
};

enum mosquitto_msg_state {
	mosq_ms_invalid = 0,
	mosq_ms_wait_puback = 1,
	mosq_ms_wait_pubrec = 2,
	mosq_ms_wait_pubrel = 3,
	mosq_ms_wait_pubcomp = 4
};

struct mosquitto_message{
	struct mosquitto_message *next;
	time_t timestamp;
	enum mosquitto_msg_direction direction;
	enum mosquitto_msg_state state;
	uint16_t mid;
	char *topic;
	uint8_t *payload;
	uint32_t payloadlen;
	int qos;
	bool retain;
	bool dup;
};

struct mosquitto;

mosq_EXPORT int mosquitto_lib_init(void);
mosq_EXPORT int mosquitto_lib_cleanup(void);

mosq_EXPORT struct mosquitto *mosquitto_new(void *obj, const char *id);
mosq_EXPORT int mosquitto_will_set(struct mosquitto *mosq, bool will, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain);
mosq_EXPORT void mosquitto_destroy(struct mosquitto *mosq);
mosq_EXPORT int mosquitto_connect(struct mosquitto *mosq, const char *host, int port, int keepalive, bool clean_session);
mosq_EXPORT int mosquitto_disconnect(struct mosquitto *mosq);
mosq_EXPORT int mosquitto_publish(struct mosquitto *mosq, uint16_t *mid, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain);
mosq_EXPORT int mosquitto_subscribe(struct mosquitto *mosq, const char *sub, int qos);
mosq_EXPORT int mosquitto_unsubscribe(struct mosquitto *mosq, const char *sub);
mosq_EXPORT int mosquitto_loop(struct mosquitto *mosq, int timeout);
mosq_EXPORT int mosquitto_read(struct mosquitto *mosq);
mosq_EXPORT int mosquitto_write(struct mosquitto *mosq);

mosq_EXPORT void mosquitto_connect_callback_set(struct mosquitto *mosq, void (*on_connect)(void *, int));
mosq_EXPORT void mosquitto_publish_callback_set(struct mosquitto *mosq, void (*on_publish)(void *, uint16_t));
mosq_EXPORT void mosquitto_message_callback_set(struct mosquitto *mosq, void (*on_message)(void *, struct mosquitto_message *));
mosq_EXPORT void mosquitto_subscribe_callback_set(struct mosquitto *mosq, void (*on_subscribe)(void *, uint16_t, int, uint8_t *));
mosq_EXPORT void mosquitto_unsubscribe_callback_set(struct mosquitto *mosq, void (*on_unsubscribe)(void *, uint16_t));

mosq_EXPORT void mosquitto_message_retry_check(struct mosquitto *mosq);
mosq_EXPORT void mosquitto_message_retry_set(struct mosquitto *mosq, unsigned int message_retry);
mosq_EXPORT void mosquitto_message_cleanup(struct mosquitto_message **message);

mosq_EXPORT int mosquitto_log_init(struct mosquitto *mosq, int priorities, int destinations);

#ifdef __cplusplus
}
#endif

#endif
