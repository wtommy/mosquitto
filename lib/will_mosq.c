/*
Copyright (c) 2010,2011 Roger Light <roger@atchoo.org>
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
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifndef WIN32
#include <sys/select.h>
#include <unistd.h>
#else
#include <winsock2.h>
typedef int ssize_t;
#endif

#include <mosquitto.h>
#include <mosquitto_internal.h>
#include <logging_mosq.h>
#include <messages_mosq.h>
#include <memory_mosq.h>
#include <mqtt3_protocol.h>
#include <net_mosq.h>
#include <read_handle.h>
#include <send_mosq.h>
#include <util_mosq.h>

int _mosquitto_will_set(struct _mosquitto_core *core, bool will, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain)
{
	int rc = MOSQ_ERR_SUCCESS;

	if(!core || (will && !topic)) return MOSQ_ERR_INVAL;
	if(payloadlen > 268435455) return MOSQ_ERR_PAYLOAD_SIZE;

	if(core->will){
		if(core->will->topic){
			_mosquitto_free(core->will->topic);
			core->will->topic = NULL;
		}
		if(core->will->payload){
			_mosquitto_free(core->will->payload);
			core->will->payload = NULL;
		}
		_mosquitto_free(core->will);
		core->will = NULL;
	}

	if(will){
		core->will = _mosquitto_calloc(1, sizeof(struct mosquitto_message));
		if(!core->will) return MOSQ_ERR_NOMEM;
		core->will->topic = _mosquitto_strdup(topic);
		if(!core->will->topic){
			rc = MOSQ_ERR_NOMEM;
			goto cleanup;
		}
		core->will->payloadlen = payloadlen;
		if(core->will->payloadlen > 0){
			if(!payload){
				rc = MOSQ_ERR_INVAL;
				goto cleanup;
			}
			core->will->payload = _mosquitto_malloc(sizeof(uint8_t)*core->will->payloadlen);
			if(!core->will->payload){
				rc = MOSQ_ERR_NOMEM;
				goto cleanup;
			}

			memcpy(core->will->payload, payload, payloadlen);
		}
		core->will->qos = qos;
		core->will->retain = retain;
	}

	return MOSQ_ERR_SUCCESS;

cleanup:
	if(core->will){
		if(core->will->topic) _mosquitto_free(core->will->topic);
		if(core->will->payload) _mosquitto_free(core->will->payload);
	}
	_mosquitto_free(core->will);
	core->will = NULL;

	return rc;
}

