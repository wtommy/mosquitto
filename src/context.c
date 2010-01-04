/*
Copyright (c) 2009, Roger Light <roger@atchoo.org>
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

#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/socket.h>

#include <mqtt3.h>

mqtt3_context *mqtt3_context_init(int sock)
{
	mqtt3_context *context;
	struct sockaddr addr;
	socklen_t addrlen;
	char address[1024];

	context = mqtt3_malloc(sizeof(mqtt3_context));
	if(!context) return NULL;
	
	context->sock = sock;
	context->last_msg_in = time(NULL);
	context->last_msg_out = time(NULL);
	context->keepalive = 60; /* Default to 60s */
	context->clean_start = true;
	context->id = NULL;

	context->in_packet.command = 0;
	context->in_packet.have_remaining = 0;
	context->in_packet.remaining_count = 0;
	context->in_packet.remaining_mult = 1;
	context->in_packet.remaining_length = 0;
	context->in_packet.payload = NULL;
	context->in_packet.to_read = 0;
	context->in_packet.pos = 0;

	addrlen = sizeof(addr);
	context->address = NULL;
	if(!getpeername(sock, &addr, &addrlen)){
		if(inet_ntop(AF_INET, &((struct sockaddr_in *)&addr)->sin_addr.s_addr, address, 1024)){
			context->address = mqtt3_strdup(address);
		}
	}
	
	return context;
}

void mqtt3_context_cleanup(mqtt3_context *context)
{
	if(!context) return;

	if(context->sock != -1){
		mqtt3_socket_close(context);
	}
	if(context->clean_start){
		mqtt3_db_subs_clean_start(context->id);
		mqtt3_db_messages_delete(context->id);
		mqtt3_db_client_delete(context);
	}
	if(context->address) mqtt3_free(context->address);
	if(context->id) mqtt3_free(context->id);
	mqtt3_context_packet_cleanup(&(context->in_packet));
	mqtt3_free(context);
}

void mqtt3_context_packet_cleanup(struct _mqtt3_packet *packet)
{
	if(!packet) return;

	/* Free data and reset values */
	packet->command = 0;
	packet->have_remaining = 0;
	packet->remaining_count = 0;
	packet->remaining_mult = 1;
	packet->remaining_length = 0;
	if(packet->payload) mqtt3_free(packet->payload);
	packet->payload = NULL;
	packet->to_read = 0;
	packet->pos = 0;
}

