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

#ifndef WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#else
#include <ws2tcpip.h>
#endif

#include <config.h>
#include <mqtt3.h>
#include <memory_mosq.h>

mqtt3_context *mqtt3_context_init(int sock)
{
	mqtt3_context *context;
	struct sockaddr_storage addr;
	socklen_t addrlen;
	char address[1024];

	context = _mosquitto_malloc(sizeof(mqtt3_context));
	if(!context) return NULL;
	
	context->core.state = mosq_cs_new;
	context->duplicate = false;
	context->core.sock = sock;
	context->core.last_msg_in = time(NULL);
	context->core.last_msg_out = time(NULL);
	context->core.keepalive = 60; /* Default to 60s */
	context->clean_session = true;
	context->core.id = NULL;
	context->core.last_mid = 0;
	context->core.will = NULL;
	context->core.username = NULL;
	context->core.password = NULL;

	context->core.in_packet.payload = NULL;
	_mosquitto_packet_cleanup(&context->core.in_packet);
	context->core.out_packet = NULL;

	addrlen = sizeof(addr);
	context->address = NULL;
	if(!getpeername(sock, (struct sockaddr *)&addr, &addrlen)){
		if(addr.ss_family == AF_INET){
			if(inet_ntop(AF_INET, &((struct sockaddr_in *)&addr)->sin_addr.s_addr, address, 1024)){
				context->address = _mosquitto_strdup(address);
			}
		}else if(addr.ss_family == AF_INET6){
			if(inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&addr)->sin6_addr.s6_addr, address, 1024)){
				context->address = _mosquitto_strdup(address);
			}
		}
	}
	context->bridge = NULL;
	context->msgs = NULL;
#ifdef WITH_SSL
	context->core.ssl = NULL;
#endif

	return context;
}

/*
 * This will result in any outgoing packets going unsent. If we're disconnected
 * forcefully then it is usually an error condition and shouldn't be a problem,
 * but it will mean that CONNACK messages will never get sent for bad protocol
 * versions for example.
 */
void mqtt3_context_cleanup(mosquitto_db *db, mqtt3_context *context, bool do_free)
{
	struct _mosquitto_packet *packet;
	mosquitto_client_msg *msg, *next;
	if(!context) return;

	if(context->core.sock != -1){
		mqtt3_socket_close(context);
	}
	if(context->clean_session && !context->duplicate){
		mqtt3_subs_clean_session(context, &db->subs);
		mqtt3_db_messages_delete(context);
	}
	if(context->address) _mosquitto_free(context->address);
	if(context->core.id) _mosquitto_free(context->core.id);
	_mosquitto_packet_cleanup(&(context->core.in_packet));
	while(context->core.out_packet){
		_mosquitto_packet_cleanup(context->core.out_packet);
		packet = context->core.out_packet;
		context->core.out_packet = context->core.out_packet->next;
		_mosquitto_free(packet);
	}
	if(context->core.will){
		if(context->core.will->topic) _mosquitto_free(context->core.will->topic);
		if(context->core.will->payload) _mosquitto_free(context->core.will->payload);
		_mosquitto_free(context->core.will);
	}
	msg = context->msgs;
	while(msg){
		next = msg->next;
		msg->store->ref_count--;
		_mosquitto_free(msg);
		msg = next;
	}
	if(do_free){
		_mosquitto_free(context);
	}
}

