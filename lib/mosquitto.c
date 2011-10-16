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
#include <will_mosq.h>

#ifndef ECONNRESET
#define ECONNRESET 104
#endif

#if !defined(WIN32) && defined(__SYMBIAN32__)
#define HAVE_PSELECT
#endif

void mosquitto_lib_version(int *major, int *minor, int *revision)
{
	if(major) *major = LIBMOSQUITTO_MAJOR;
	if(minor) *minor = LIBMOSQUITTO_MINOR;
	if(revision) *revision = LIBMOSQUITTO_REVISION;
}

int mosquitto_lib_init(void)
{
	_mosquitto_net_init();

	return MOSQ_ERR_SUCCESS;
}

int mosquitto_lib_cleanup(void)
{
	_mosquitto_net_cleanup();

	return MOSQ_ERR_SUCCESS;
}

struct mosquitto *mosquitto_new(const char *id, void *obj)
{
	struct mosquitto *mosq = NULL;

	if(!id) return NULL;

	mosq = (struct mosquitto *)_mosquitto_calloc(1, sizeof(struct mosquitto));
	if(mosq){
		if(obj){
			mosq->obj = obj;
		}else{
			mosq->obj = mosq;
		}
		mosq->core.sock = INVALID_SOCKET;
		mosq->core.keepalive = 60;
		mosq->message_retry = 20;
		mosq->last_retry_check = 0;
		mosq->core.id = _mosquitto_strdup(id);
		mosq->core.username = NULL;
		mosq->core.password = NULL;
		mosq->core.in_packet.payload = NULL;
		_mosquitto_packet_cleanup(&mosq->core.in_packet);
		mosq->core.out_packet = NULL;
		mosq->core.last_msg_in = time(NULL);
		mosq->core.last_msg_out = time(NULL);
		mosq->core.last_mid = 0;
		mosq->core.state = mosq_cs_new;
		mosq->messages = NULL;
		mosq->core.will = NULL;
		mosq->on_connect = NULL;
		mosq->on_publish = NULL;
		mosq->on_message = NULL;
		mosq->on_subscribe = NULL;
		mosq->on_unsubscribe = NULL;
		mosq->log_destinations = MOSQ_LOG_NONE;
		mosq->log_priorities = MOSQ_LOG_ERR | MOSQ_LOG_WARNING | MOSQ_LOG_NOTICE | MOSQ_LOG_INFO;
		mosq->host = NULL;
		mosq->port = 1883;
#ifdef WITH_SSL
		mosq->core.ssl = NULL;
#endif
	}
	return mosq;
}

int mosquitto_will_set(struct mosquitto *mosq, bool will, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain)
{
	if(!mosq) return MOSQ_ERR_INVAL;
	return _mosquitto_will_set(&mosq->core, will, topic, payloadlen, payload, qos, retain);
}

int mosquitto_username_pw_set(struct mosquitto *mosq, const char *username, const char *password)
{
	if(!mosq) return MOSQ_ERR_INVAL;

	if(username){
		mosq->core.username = _mosquitto_strdup(username);
		if(!mosq->core.username) return MOSQ_ERR_NOMEM;
		if(mosq->core.password){
			_mosquitto_free(mosq->core.password);
			mosq->core.password = NULL;
		}
		if(password){
			mosq->core.password = _mosquitto_strdup(password);
			if(!mosq->core.password){
				_mosquitto_free(mosq->core.username);
				mosq->core.username = NULL;
				return MOSQ_ERR_NOMEM;
			}
		}
	}else{
		if(mosq->core.username){
			_mosquitto_free(mosq->core.username);
			mosq->core.username = NULL;
		}
		if(mosq->core.password){
			_mosquitto_free(mosq->core.password);
			mosq->core.password = NULL;
		}
	}
	return MOSQ_ERR_SUCCESS;
}


void mosquitto_destroy(struct mosquitto *mosq)
{
	if(mosq->core.id) _mosquitto_free(mosq->core.id);
	_mosquitto_message_cleanup_all(mosq);
	if(mosq->core.will){
		if(mosq->core.will->topic) _mosquitto_free(mosq->core.will->topic);
		if(mosq->core.will->payload) _mosquitto_free(mosq->core.will->payload);
		_mosquitto_free(mosq->core.will);
	}
	if(mosq->host){
		_mosquitto_free(mosq->host);
	}
#ifdef WITH_SSL
	if(mosq->core.ssl){
		if(mosq->core.ssl->ssl){
			SSL_free(mosq->core.ssl->ssl);
		}
		if(mosq->core.ssl->ssl_ctx){
			SSL_CTX_free(mosq->core.ssl->ssl_ctx);
		}
		_mosquitto_free(mosq->core.ssl);
	}
#endif
	_mosquitto_free(mosq);
}

int mosquitto_socket(struct mosquitto *mosq)
{
	if(!mosq) return MOSQ_ERR_INVAL;
	return mosq->core.sock;
}

int mosquitto_connect(struct mosquitto *mosq, const char *host, int port, int keepalive, bool clean_session)
{
	if(!mosq) return MOSQ_ERR_INVAL;
	if(!host || port <= 0) return MOSQ_ERR_INVAL;

	if(mosq->host) _mosquitto_free(mosq->host);
	mosq->host = _mosquitto_strdup(host);
	if(!mosq->host) return MOSQ_ERR_NOMEM;
	mosq->port = port;

	mosq->core.keepalive = keepalive;
	mosq->core.clean_session = clean_session;

	return mosquitto_reconnect(mosq);
}

int mosquitto_reconnect(struct mosquitto *mosq)
{
	int rc;
	if(!mosq) return MOSQ_ERR_INVAL;
	if(!mosq->host || mosq->port <= 0) return MOSQ_ERR_INVAL;

	rc = _mosquitto_socket_connect(&mosq->core, mosq->host, mosq->port);
	if(rc){
		return rc;
	}

	return _mosquitto_send_connect(&mosq->core, mosq->core.keepalive, mosq->core.clean_session);
}

int mosquitto_disconnect(struct mosquitto *mosq)
{
	if(!mosq) return MOSQ_ERR_INVAL;
	if(mosq->core.sock == INVALID_SOCKET) return MOSQ_ERR_NO_CONN;

	mosq->core.state = mosq_cs_disconnecting;

	return _mosquitto_send_disconnect(&mosq->core);
}

int mosquitto_publish(struct mosquitto *mosq, uint16_t *mid, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain)
{
	struct mosquitto_message_all *message;
	uint16_t local_mid;

	if(!mosq || !topic || qos<0 || qos>2) return MOSQ_ERR_INVAL;
	if(payloadlen > 268435455) return MOSQ_ERR_PAYLOAD_SIZE;

	if(_mosquitto_wildcard_check(topic)){
		return MOSQ_ERR_INVAL;
	}

	local_mid = _mosquitto_mid_generate(&mosq->core);
	if(mid){
		*mid = local_mid;
	}

	if(qos == 0){
		return _mosquitto_send_publish(mosq, local_mid, topic, payloadlen, payload, qos, retain, false);
	}else{
		message = _mosquitto_calloc(1, sizeof(struct mosquitto_message_all));
		if(!message) return MOSQ_ERR_NOMEM;

		message->next = NULL;
		message->timestamp = time(NULL);
		message->direction = mosq_md_out;
		if(qos == 1){
			message->state = mosq_ms_wait_puback;
		}else if(qos == 2){
			message->state = mosq_ms_wait_pubrec;
		}
		message->msg.mid = local_mid;
		message->msg.topic = _mosquitto_strdup(topic);
		if(!message->msg.topic){
			_mosquitto_message_cleanup(&message);
			return MOSQ_ERR_NOMEM;
		}
		if(payloadlen){
			message->msg.payloadlen = payloadlen;
			message->msg.payload = _mosquitto_malloc(payloadlen*sizeof(uint8_t));
			if(!message){
				_mosquitto_message_cleanup(&message);
				return MOSQ_ERR_NOMEM;
			}
			memcpy(message->msg.payload, payload, payloadlen*sizeof(uint8_t));
		}else{
			message->msg.payloadlen = 0;
			message->msg.payload = NULL;
		}
		message->msg.qos = qos;
		message->msg.retain = retain;
		message->dup = false;

		_mosquitto_message_queue(mosq, message);
		return _mosquitto_send_publish(mosq, message->msg.mid, message->msg.topic, message->msg.payloadlen, message->msg.payload, message->msg.qos, message->msg.retain, message->dup);
	}
}

int mosquitto_subscribe(struct mosquitto *mosq, uint16_t *mid, const char *sub, int qos)
{
	if(!mosq) return MOSQ_ERR_INVAL;
	if(mosq->core.sock == INVALID_SOCKET) return MOSQ_ERR_NO_CONN;

	return _mosquitto_send_subscribe(&mosq->core, mid, false, sub, qos);
}

int mosquitto_unsubscribe(struct mosquitto *mosq, uint16_t *mid, const char *sub)
{
	if(!mosq) return MOSQ_ERR_INVAL;
	if(mosq->core.sock == INVALID_SOCKET) return MOSQ_ERR_NO_CONN;

	return _mosquitto_send_unsubscribe(&mosq->core, mid, false, sub);
}

#if 0
int mosquitto_ssl_set(struct mosquitto *mosq, const char *pemfile, const char *password)
{
#ifdef WITH_SSL
	if(!mosq || mosq->core.ssl) return MOSQ_ERR_INVAL; //FIXME

	mosq->core.ssl = _mosquitto_malloc(sizeof(struct _mosquitto_ssl));
	if(!mosq->core.ssl) return MOSQ_ERR_NOMEM;

	mosq->core.ssl->ssl_ctx = SSL_CTX_new(TLSv1_method());
	if(!mosq->core.ssl->ssl_ctx) return MOSQ_ERR_SSL;

	mosq->core.ssl->ssl = SSL_new(mosq->core.ssl->ssl_ctx);

	return MOSQ_ERR_SUCCESS;
#else
	return MOSQ_ERR_NOT_SUPPORTED;
#endif
}
#endif

int mosquitto_loop(struct mosquitto *mosq, int timeout)
{
#ifdef HAVE_PSELECT
	struct timespec local_timeout;
#else
	struct timeval local_timeout;
#endif
	fd_set readfds, writefds;
	int fdcount;
	int rc;

	if(!mosq) return MOSQ_ERR_INVAL;
	if(mosq->core.sock == INVALID_SOCKET) return MOSQ_ERR_NO_CONN;

	FD_ZERO(&readfds);
	FD_SET(mosq->core.sock, &readfds);
	FD_ZERO(&writefds);
	if(mosq->core.out_packet){
		FD_SET(mosq->core.sock, &writefds);
#ifdef WITH_SSL
	}else if(mosq->core.ssl && mosq->core.ssl->want_write){
		FD_SET(mosq->core.sock, &writefds);
#endif
	}
	if(timeout >= 0){
		local_timeout.tv_sec = timeout/1000;
#ifdef HAVE_PSELECT
		local_timeout.tv_nsec = (timeout-local_timeout.tv_sec*1000)*1e6;
#else
		local_timeout.tv_usec = (timeout-local_timeout.tv_sec*1000)*1000;
#endif
	}else{
		local_timeout.tv_sec = 1;
#ifdef HAVE_PSELECT
		local_timeout.tv_nsec = 0;
#else
		local_timeout.tv_usec = 0;
#endif
	}

#ifdef HAVE_PSELECT
	fdcount = pselect(mosq->core.sock+1, &readfds, &writefds, NULL, &local_timeout, NULL);
#else
	fdcount = select(mosq->core.sock+1, &readfds, &writefds, NULL, &local_timeout);
#endif
	if(fdcount == -1){
		return MOSQ_ERR_UNKNOWN; // FIXME what error to return?
	}else{
		if(FD_ISSET(mosq->core.sock, &readfds)){
			rc = mosquitto_loop_read(mosq);
			if(rc){
				_mosquitto_socket_close(&mosq->core);
				if(mosq->core.state == mosq_cs_disconnecting){
					rc = MOSQ_ERR_SUCCESS;
				}
				if(mosq->on_disconnect){
					mosq->on_disconnect(mosq->obj);
				}
				return rc;
			}
		}
		if(FD_ISSET(mosq->core.sock, &writefds)){
			rc = mosquitto_loop_write(mosq);
			if(rc){
				_mosquitto_socket_close(&mosq->core);
				if(mosq->core.state == mosq_cs_disconnecting){
					rc = MOSQ_ERR_SUCCESS;
				}
				if(mosq->on_disconnect){
					mosq->on_disconnect(mosq->obj);
				}
				return rc;
			}
		}
	}
	mosquitto_loop_misc(mosq);

	return MOSQ_ERR_SUCCESS;
}

int mosquitto_loop_misc(struct mosquitto *mosq)
{
	if(!mosq) return MOSQ_ERR_INVAL;

	_mosquitto_check_keepalive(mosq);
	if(mosq->last_retry_check+1 < time(NULL)){
		_mosquitto_message_retry_check(mosq);
		mosq->last_retry_check = time(NULL);
	}
	return MOSQ_ERR_SUCCESS;
}

int mosquitto_loop_read(struct mosquitto *mosq)
{
	return _mosquitto_packet_read(mosq);
}

int mosquitto_loop_write(struct mosquitto *mosq)
{
	return _mosquitto_packet_write(mosq);
}

void mosquitto_connect_callback_set(struct mosquitto *mosq, void (*on_connect)(void *, int))
{
	mosq->on_connect = on_connect;
}

void mosquitto_disconnect_callback_set(struct mosquitto *mosq, void (*on_disconnect)(void *))
{
	mosq->on_disconnect = on_disconnect;
}

void mosquitto_publish_callback_set(struct mosquitto *mosq, void (*on_publish)(void *, uint16_t))
{
	mosq->on_publish = on_publish;
}

void mosquitto_message_callback_set(struct mosquitto *mosq, void (*on_message)(void *, const struct mosquitto_message *))
{
	mosq->on_message = on_message;
}

void mosquitto_subscribe_callback_set(struct mosquitto *mosq, void (*on_subscribe)(void *, uint16_t, int, const uint8_t *))
{
	mosq->on_subscribe = on_subscribe;
}

void mosquitto_unsubscribe_callback_set(struct mosquitto *mosq, void (*on_unsubscribe)(void *, uint16_t))
{
	mosq->on_unsubscribe = on_unsubscribe;
}

