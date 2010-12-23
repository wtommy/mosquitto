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

#include <config.h>

#ifndef WIN32
#include <netdb.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#ifdef WITH_WRAP
#include <tcpd.h>
#endif

#include <mqtt3.h>
#include <mqtt3_protocol.h>
#include <memory_mosq.h>

static uint64_t bytes_received = 0;
static uint64_t bytes_sent = 0;
static unsigned long msgs_received = 0;
static unsigned long msgs_sent = 0;
static int max_connections = -1;

void mqtt3_net_set_max_connections(int max)
{
	max_connections = max;
}

int mqtt3_socket_accept(mqtt3_context ***contexts, int *context_count, int listensock)
{
	int i;
	int new_sock = -1;
	mqtt3_context **tmp_contexts = NULL;
	mqtt3_context *new_context;
	int opt = 1;
#ifdef WITH_WRAP
	struct request_info wrap_req;
#endif

	new_sock = accept(listensock, NULL, 0);
	if(new_sock < 0) return -1;

	if(max_connections > 0 && (*context_count) >= max_connections){
#ifndef WIN32
		close(new_sock);
#else
		closesocket(new_sock);
#endif
		return -1;
	}
#ifndef WIN32
	/* Set non-blocking */
	opt = fcntl(new_sock, F_GETFL, 0);
	if(opt == -1 || fcntl(new_sock, F_SETFL, opt | O_NONBLOCK) == -1){
		/* If either fcntl fails, don't want to allow this client to connect. */
		close(new_sock);
		return -1;
	}
#else
	if(ioctlsocket(new_sock, FIONBIO, &opt)){
		closesocket(new_sock);
		return INVALID_SOCKET;
	}
#endif

#ifdef WITH_WRAP
	/* Use tcpd / libwrap to determine whether a connection is allowed. */
	request_init(&wrap_req, RQ_FILE, new_sock, RQ_DAEMON, "mosquitto", 0);
	fromhost(&wrap_req);
	if(!hosts_access(&wrap_req)){
		/* Access is denied */
		mqtt3_log_printf(MOSQ_LOG_NOTICE, "Client connection denied access by tcpd.");
#ifndef WIN32
		close(new_sock);
#else
		closesocket(new_sock);
#endif
		return -1;
	}else{
#endif
		new_context = mqtt3_context_init(new_sock);
		if(!new_context) return -1;
		mqtt3_log_printf(MOSQ_LOG_NOTICE, "New client connected from %s.", new_context->address);
		for(i=0; i<(*context_count); i++){
			if((*contexts)[i] == NULL){
				(*contexts)[i] = new_context;
				break;
			}
		}
		if(i==(*context_count)){
			(*context_count)++;
			tmp_contexts = _mosquitto_realloc(*contexts, sizeof(mqtt3_context*)*(*context_count));
			if(tmp_contexts){
				*contexts = tmp_contexts;
				(*contexts)[(*context_count)-1] = new_context;
			}
		}
#ifdef WITH_WRAP
	}
#endif
	return new_sock;
}

/* Close a socket associated with a context and set it to -1.
 */
void mqtt3_socket_close(mqtt3_context *context)
{
	int rc = 0;

	assert(context);

	if(context->core.sock != -1){
#ifndef WIN32
		rc = close(context->core.sock);
#else
		rc = closesocket(context->core.sock);
#endif
		context->core.sock = -1;
	}
}

/* Creates a socket and listens on port 'port'.
 * Returns 1 on failure
 * Returns 0 on success.
 */
int mqtt3_socket_listen(const char *host, uint16_t port, int **socks, int *sock_count)
{
	int sock = -1;
	struct addrinfo hints;
	struct addrinfo *ainfo, *rp;
	char service[10];
	int opt = 1;
#ifndef WIN32
	int ss_opt = 1;
#else
	char ss_opt = 1;
#endif

	snprintf(service, 10, "%d", port);
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = PF_UNSPEC;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;

	if(getaddrinfo(host, service, &hints, &ainfo)) return INVALID_SOCKET;

	*sock_count = 0;
	*socks = NULL;

	for(rp = ainfo; rp; rp = rp->ai_next){
		if(rp->ai_family == AF_INET){
			mqtt3_log_printf(MOSQ_LOG_INFO, "Opening ipv4 listen socket on port %d.", ntohs(((struct sockaddr_in *)rp->ai_addr)->sin_port));
		}else if(rp->ai_family == AF_INET6){
			mqtt3_log_printf(MOSQ_LOG_INFO, "Opening ipv6 listen socket on port %d.", ntohs(((struct sockaddr_in6 *)rp->ai_addr)->sin6_port));
		}else{
			continue;
		}

		sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if(sock == -1){
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s", strerror(errno));
			return 1;
		}
		(*sock_count)++;
		*socks = _mosquitto_realloc(*socks, sizeof(int)*(*sock_count));
		if(!(*socks)){
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory.");
			return MOSQ_ERR_NOMEM;
		}
		(*socks)[(*sock_count)-1] = sock;

		ss_opt = 1;
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &ss_opt, sizeof(ss_opt));
		ss_opt = 1;
		setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &ss_opt, sizeof(ss_opt));


#ifndef WIN32
		/* Set non-blocking */
		opt = fcntl(sock, F_GETFL, 0);
		if(opt == -1 || fcntl(sock, F_SETFL, opt | O_NONBLOCK) == -1){
			/* If either fcntl fails, don't want to allow this client to connect. */
			close(sock);
			return 1;
		}
#else
		if(ioctlsocket(sock, FIONBIO, &opt)){
			closesocket(sock);
			return 1;
		}
#endif

		if(bind(sock, rp->ai_addr, rp->ai_addrlen) == -1){
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s", strerror(errno));
#ifndef WIN32
			close(sock);
#else
			closesocket(sock);
#endif
			return 1;
		}

		if(listen(sock, 100) == -1){
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s", strerror(errno));
#ifndef WIN32
			close(sock);
#else
			closesocket(sock);
#endif
			return 1;
		}
	}
	freeaddrinfo(ainfo);

	return 0;
}

int mqtt3_net_packet_queue(mqtt3_context *context, struct _mosquitto_packet *packet)
{
	struct _mosquitto_packet *tail;

	if(!context || !packet) return MOSQ_ERR_INVAL;

	packet->next = NULL;
	if(context->core.out_packet){
		tail = context->core.out_packet;
		while(tail->next){
			tail = tail->next;
		}
		tail->next = packet;
	}else{
		context->core.out_packet = packet;
	}
	return MOSQ_ERR_SUCCESS;
}

int mqtt3_net_read(mosquitto_db *db, mqtt3_context *context)
{
	uint8_t byte;
	ssize_t read_length;
	int rc = 0;

	if(!context || context->core.sock == -1) return MOSQ_ERR_INVAL;
	/* This gets called if pselect() indicates that there is network data
	 * available - ie. at least one byte.  What we do depends on what data we
	 * already have.
	 * If we've not got a command, attempt to read one and save it. This should
	 * always work because it's only a single byte.
	 * Then try to read the remaining length. This may fail because it is may
	 * be more than one byte - will need to save data pending next read if it
	 * does fail.
	 * Then try to read the remaining payload, where 'payload' here means the
	 * combined variable header and actual payload. This is the most likely to
	 * fail due to longer length, so save current data and current position.
	 * After all data is read, send to mqtt3_handle_packet() to deal with.
	 * Finally, free the memory and reset everything to starting conditions.
	 */
	if(!context->core.in_packet.command){
		/* FIXME - check command and fill in expected length if we know it.
		 * This means we can check the client is sending valid data some times.
		 */
		read_length = _mosquitto_net_read(&context->core, &byte, 1);
		if(read_length == 1){
			bytes_received++;
			context->core.in_packet.command = byte;
#ifdef WITH_BROKER
			/* Clients must send CONNECT as their first command. */
			if(!(context->bridge) && context->core.state == mosq_cs_new && (byte&0xF0) != CONNECT) return 1;
#endif
		}else{
			if(read_length == 0) return 1; /* EOF */
#ifndef WIN32
			if(errno == EAGAIN || errno == EWOULDBLOCK){
#else
			if(WSAGetLastError() == WSAEWOULDBLOCK){
#endif
				return MOSQ_ERR_SUCCESS;
			}else{
				return 1;
			}
		}
	}
	if(!context->core.in_packet.have_remaining){
		/* Read remaining
		 * Algorithm for decoding taken from pseudo code at
		 * http://publib.boulder.ibm.com/infocenter/wmbhelp/v6r0m0/topic/com.ibm.etools.mft.doc/ac10870_.htm
		 */
		do{
			read_length = _mosquitto_net_read(&context->core, &byte, 1);
			if(read_length == 1){
				context->core.in_packet.remaining_count++;
				/* Max 4 bytes length for remaining length as defined by protocol.
				 * Anything more likely means a broken/malicious client.
				 */
				if(context->core.in_packet.remaining_count > 4) return MOSQ_ERR_PROTOCOL;

				bytes_received++;
				context->core.in_packet.remaining_length += (byte & 127) * context->core.in_packet.remaining_mult;
				context->core.in_packet.remaining_mult *= 128;
			}else{
				if(read_length == 0) return 1; /* EOF */
#ifndef WIN32
				if(errno == EAGAIN || errno == EWOULDBLOCK){
#else
				if(WSAGetLastError() == WSAEWOULDBLOCK){
#endif
					return MOSQ_ERR_SUCCESS;
				}else{
					return 1;
				}
			}
		}while((byte & 128) != 0);

		if(context->core.in_packet.remaining_length > 0){
			context->core.in_packet.payload = _mosquitto_malloc(context->core.in_packet.remaining_length*sizeof(uint8_t));
			if(!context->core.in_packet.payload) return MOSQ_ERR_NOMEM;
			context->core.in_packet.to_process = context->core.in_packet.remaining_length;
		}
		context->core.in_packet.have_remaining = 1;
	}
	while(context->core.in_packet.to_process>0){
		read_length = _mosquitto_net_read(&context->core, &(context->core.in_packet.payload[context->core.in_packet.pos]), context->core.in_packet.to_process);
		if(read_length > 0){
			bytes_received += read_length;
			context->core.in_packet.to_process -= read_length;
			context->core.in_packet.pos += read_length;
		}else{
#ifndef WIN32
			if(errno == EAGAIN || errno == EWOULDBLOCK){
#else
			if(WSAGetLastError() == WSAEWOULDBLOCK){
#endif
				return MOSQ_ERR_SUCCESS;
			}else{
				return 1;
			}
		}
	}

	msgs_received++;
	/* All data for this packet is read. */
	context->core.in_packet.pos = 0;
	rc = mqtt3_packet_handle(db, context);

	/* Free data and reset values */
	_mosquitto_packet_cleanup(&context->core.in_packet);

	context->core.last_msg_in = time(NULL);
	return rc;
}

int mqtt3_net_write(mqtt3_context *context)
{
	uint8_t byte;
	ssize_t write_length;
	struct _mosquitto_packet *packet;

	if(!context || context->core.sock == -1) return MOSQ_ERR_INVAL;

	while(context->core.out_packet){
		packet = context->core.out_packet;

		if(packet->command){
			/* Assign to_proces here before remaining_length changes. */
			packet->to_process = packet->remaining_length;
			packet->pos = 0;

			write_length = _mosquitto_net_write(&context->core, &packet->command, 1);
			if(write_length == 1){
				bytes_sent++;
				packet->command = 0;
			}else{
				if(write_length == 0) return 1; /* EOF */
#ifndef WIN32
				if(errno == EAGAIN || errno == EWOULDBLOCK){
#else
				if(WSAGetLastError() == WSAEWOULDBLOCK){
#endif
					return MOSQ_ERR_SUCCESS;
				}else{
					return 1;
				}
			}
		}
		if(!packet->have_remaining){
			/* Write remaining
			 * Algorithm for encoding taken from pseudo code at
			 * http://publib.boulder.ibm.com/infocenter/wmbhelp/v6r0m0/topic/com.ibm.etools.mft.doc/ac10870_.htm
			 */
			do{
				byte = packet->remaining_length % 128;
				packet->remaining_length = packet->remaining_length / 128;
				/* If there are more digits to encode, set the top bit of this digit */
				if(packet->remaining_length>0){
					byte = byte | 0x80;
				}
				write_length = _mosquitto_net_write(&context->core, &byte, 1);
				if(write_length == 1){
					packet->remaining_count++;
					/* Max 4 bytes length for remaining length as defined by protocol. */
					if(packet->remaining_count > 4) return MOSQ_ERR_PROTOCOL;
	
					bytes_sent++;
				}else{
					if(write_length == 0) return 1; /* EOF */
#ifndef WIN32
					if(errno == EAGAIN || errno == EWOULDBLOCK){
#else
					if(WSAGetLastError() == WSAEWOULDBLOCK){
#endif
						return MOSQ_ERR_SUCCESS;
					}else{
						return 1;
					}
				}
			}while(packet->remaining_length > 0);
			packet->have_remaining = 1;
		}
		while(packet->to_process > 0){
			write_length = _mosquitto_net_write(&context->core, &(packet->payload[packet->pos]), packet->to_process);
			if(write_length > 0){
				bytes_sent += write_length;
				packet->to_process -= write_length;
				packet->pos += write_length;
			}else{
#ifndef WIN32
				if(errno == EAGAIN || errno == EWOULDBLOCK){
#else
				if(WSAGetLastError() == WSAEWOULDBLOCK){
#endif
					return MOSQ_ERR_SUCCESS;
				}else{
					return 1;
				}
			}
		}

		msgs_sent++;

		/* Free data and reset values */
		context->core.out_packet = packet->next;
		_mosquitto_packet_cleanup(packet);
		_mosquitto_free(packet);

		context->core.last_msg_out = time(NULL);
	}
	return MOSQ_ERR_SUCCESS;
}

uint64_t mqtt3_net_bytes_total_received(void)
{
	return bytes_received;
}

uint64_t mqtt3_net_bytes_total_sent(void)
{
	return bytes_sent;
}

unsigned long mqtt3_net_msgs_total_received(void)
{
	return msgs_received;
}

unsigned long mqtt3_net_msgs_total_sent(void)
{
	return msgs_sent;
}

