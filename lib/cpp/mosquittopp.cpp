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

#include <stdint.h>
#include <cstdlib>
#include <mosquitto.h>
#include <mosquittopp.h>

static void on_connect_wrapper(void *obj, int rc)
{
	class mosquittopp *m = (class mosquittopp *)obj;
	m->on_connect(rc);
}

static void on_disconnect_wrapper(void *obj)
{
	class mosquittopp *m = (class mosquittopp *)obj;
	m->on_disconnect();
}

static void on_publish_wrapper(void *obj, uint16_t mid)
{
	class mosquittopp *m = (class mosquittopp *)obj;
	m->on_publish(mid);
}

static void on_message_wrapper(void *obj, const struct mosquitto_message *message)
{
	class mosquittopp *m = (class mosquittopp *)obj;
	m->on_message(message);
}

static void on_subscribe_wrapper(void *obj, uint16_t mid, int qos_count, const uint8_t *granted_qos)
{
	class mosquittopp *m = (class mosquittopp *)obj;
	m->on_subscribe(mid, qos_count, granted_qos);
}

static void on_unsubscribe_wrapper(void *obj, uint16_t mid)
{
	class mosquittopp *m = (class mosquittopp *)obj;
	m->on_unsubscribe(mid);
}

void mosquittopp::lib_version(int *major, int *minor, int *revision)
{
	if(major) *major = LIBMOSQUITTO_MAJOR;
	if(minor) *minor = LIBMOSQUITTO_MINOR;
	if(revision) *revision = LIBMOSQUITTO_REVISION;
}

int mosquittopp::lib_init()
{
	return mosquitto_lib_init();
}

int mosquittopp::lib_cleanup()
{
	return mosquitto_lib_cleanup();
}

mosquittopp::mosquittopp(const char *id)
{
	mosq = mosquitto_new(id, this);
	mosquitto_connect_callback_set(mosq, on_connect_wrapper);
	mosquitto_disconnect_callback_set(mosq, on_disconnect_wrapper);
	mosquitto_publish_callback_set(mosq, on_publish_wrapper);
	mosquitto_message_callback_set(mosq, on_message_wrapper);
	mosquitto_subscribe_callback_set(mosq, on_subscribe_wrapper);
	mosquitto_unsubscribe_callback_set(mosq, on_unsubscribe_wrapper);
}

mosquittopp::~mosquittopp()
{
	mosquitto_destroy(mosq);
}

int mosquittopp::connect(const char *host, int port, int keepalive, bool clean_session)
{
	return mosquitto_connect(mosq, host, port, keepalive, clean_session);
}

int mosquittopp::disconnect()
{
	return mosquitto_disconnect(mosq);
}

int mosquittopp::socket()
{
	return mosquitto_socket(mosq);
}

int mosquittopp::log_init(int priorities, int destinations)
{
	return mosquitto_log_init(mosq, priorities, destinations);
}

int mosquittopp::will_set(bool will, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain)
{
	return mosquitto_will_set(mosq, will, topic, payloadlen, payload, qos, retain);
}

int mosquittopp::username_pw_set(const char *username, const char *password)
{
	return mosquitto_username_pw_set(mosq, username, password);
}

int mosquittopp::ssl_set(const char *pemfile, const char *password)
{
	return mosquitto_ssl_set(mosq, pemfile, password);
}

int mosquittopp::publish(uint16_t *mid, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain)
{
	return mosquitto_publish(mosq, mid, topic, payloadlen, payload, qos, retain);
}

void mosquittopp::message_retry_set(unsigned int message_retry)
{
	mosquitto_message_retry_set(mosq, message_retry);
}

int mosquittopp::subscribe(uint16_t *mid, const char *sub, int qos)
{
	return mosquitto_subscribe(mosq, mid, sub, qos);
}

int mosquittopp::unsubscribe(uint16_t *mid, const char *sub)
{
	return mosquitto_unsubscribe(mosq, mid, sub);
}

int mosquittopp::loop(int timeout)
{
	return mosquitto_loop(mosq, timeout);
}

int mosquittopp::loop_misc()
{
	return mosquitto_loop_misc(mosq);
}

int mosquittopp::loop_read()
{
	return mosquitto_loop_read(mosq);
}

int mosquittopp::loop_write()
{
	return mosquitto_loop_write(mosq);
}

