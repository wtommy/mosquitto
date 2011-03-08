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

#ifndef _MOSQUITTOPP_H_
#define _MOSQUITTOPP_H_

#ifdef _WIN32
#ifdef mosquittopp_EXPORTS
#define mosqpp_EXPORT  __declspec(dllexport)
#else
#define mosqpp_EXPORT  __declspec(dllimport)
#endif
#else
#define mosqpp_EXPORT
#endif

#include <stdint.h>
#include <cstdlib>
#include <time.h>
#include <mosquitto.h>

/*
 * Class: mosquittopp
 *
 * A mosquitto client class. This is a C++ wrapper class for the mosquitto C
 * library. Please see mosquitto.h for details of the functions.
 */
class mosqpp_EXPORT mosquittopp {
	private:
		struct mosquitto *mosq;
	public:
		mosquittopp(const char *id);
		~mosquittopp();

		static void lib_version(int *major, int *minor, int *revision);
		static int lib_init();
		static int lib_cleanup();
		int socket();
		int log_init(int priorities, int destinations);
		int will_set(bool will, const char *topic, uint32_t payloadlen=0, const uint8_t *payload=NULL, int qos=0, bool retain=false);
		int username_pw_set(const char *username, const char *password=NULL);
		int connect(const char *host, int port=1883, int keepalive=60, bool clean_session=true);
		int disconnect();
		int publish(uint16_t *mid, const char *topic, uint32_t payloadlen=0, const uint8_t *payload=NULL, int qos=0, bool retain=false);
		int subscribe(uint16_t *mid, const char *sub, int qos=0);
		int unsubscribe(uint16_t *mid, const char *sub);
		void message_retry_set(unsigned int message_retry);

		int loop(int timeout=-1);
		int loop_misc();
		int loop_read();
		int loop_write();
		
		virtual void on_connect(int rc) {return;};
		virtual void on_disconnect() {return;};
		virtual void on_publish(uint16_t mid) {return;};
		virtual void on_message(const struct mosquitto_message *message) {return;};
		virtual void on_subscribe(uint16_t mid, int qos_count, const uint8_t *granted_qos) {return;};
		virtual void on_unsubscribe(uint16_t mid) {return;};
		virtual void on_error() {return;};
};

#endif
