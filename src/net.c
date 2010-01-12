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

#include <config.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#ifdef WITH_WRAP
#include <tcpd.h>
#endif
#include <unistd.h>

#include <mqtt3.h>

static uint64_t bytes_received = 0;
static uint64_t bytes_sent = 0;
static unsigned long msgs_received = 0;
static unsigned long msgs_sent = 0;

int _mqtt3_socket_listen(struct sockaddr *addr);

int mqtt3_socket_accept(mqtt3_context **contexts, int context_count, int listensock)
{
	int i;
	int new_sock = -1;
	mqtt3_context **tmp_contexts = NULL;
	mqtt3_context *new_context;
	int opt;
#ifdef WITH_WRAP
	struct request_info wrap_req;
#endif

	new_sock = accept(listensock, NULL, 0);
	if(new_sock < 0) return -1;
	/* Set non-blocking */
	opt = fcntl(new_sock, F_GETFL, 0);
	if(opt == -1 || fcntl(new_sock, F_SETFL, opt | O_NONBLOCK) == -1){
		/* If either fcntl fails, don't want to allow this client to connect. */
		close(new_sock);
		return -1;
	}
#ifdef WITH_WRAP
	/* Use tcpd / libwrap to determine whether a connection is allowed. */
	request_init(&wrap_req, RQ_FILE, new_sock, RQ_DAEMON, "mosquitto", 0);
	fromhost(&wrap_req);
	if(!hosts_access(&wrap_req)){
		/* Access is denied */
		mqtt3_log_printf(MQTT3_LOG_NOTICE, "Client connection denied access by tcpd.");
		close(new_sock);
		return -1;
	}else{
#endif
		new_context = mqtt3_context_init(new_sock);
		mqtt3_log_printf(MQTT3_LOG_NOTICE, "New client connected from %s.", new_context->address);
		for(i=0; i<context_count; i++){
			if(contexts[i] == NULL){
				contexts[i] = new_context;
				break;
			}
		}
		if(i==context_count){
			context_count++;
			tmp_contexts = mqtt3_realloc(contexts, sizeof(mqtt3_context*)*context_count);
			if(tmp_contexts){
				contexts = tmp_contexts;
				contexts[context_count-1] = new_context;
			}
		}
#ifdef WITH_WRAP
	}
#endif
	return new_sock;
}

/* Close a socket associated with a context and set it to -1.
 * Returns 1 on failure (context is NULL)
 * Returns 0 on success.
 */
int mqtt3_socket_close(mqtt3_context *context)
{
	int rc = 0;

	if(!context) return 1;
	if(context->sock != -1){
		mqtt3_db_client_invalidate_socket(context->id, context->sock);
		rc = close(context->sock);
		context->sock = -1;
	}

	return rc;
}

/* Create a socket and connect it to 'ip' on port 'port'.
 * Returns -1 on failure (ip is NULL, socket creation/connection error)
 * Returns sock number on success.
 */
int mqtt3_socket_connect(const char *ip, uint16_t port)
{
	int sock;
	struct sockaddr_in addr;
	int opt;

	if(!ip) return -1;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == -1){
		mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s", strerror(errno));
		return -1;
	}

	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_aton(ip, &(addr.sin_addr));

	if(connect(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) == -1){
		mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s", strerror(errno));
		return -1;
	}

	/* Set non-blocking */
	opt = fcntl(sock, F_GETFL, 0);
	if(opt == -1 || fcntl(sock, F_SETFL, opt | O_NONBLOCK) == -1){
		close(sock);
		return -1;
	}

	return sock;
}

/* Internal function.
 * Create a socket and set it to listen based on the sockaddr details in addr.
 * Returns -1 on failure (addr is NULL, socket creation/listening error)
 * Returns sock number on success.
 */
int _mqtt3_socket_listen(struct sockaddr *addr)
{
	int sock;
	int opt = 1;

	if(!addr) return -1;

	mqtt3_log_printf(MQTT3_LOG_INFO, "Opening listen socket on port %d.", ntohs(((struct sockaddr_in *)addr)->sin_port));

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == -1){
		mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s", strerror(errno));
		return -1;
	}

	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	/* Set non-blocking */
	opt = fcntl(sock, F_GETFL, 0);
	if(opt < 0) return -1;
	if(fcntl(sock, F_SETFL, opt | O_NONBLOCK) < 0) return -1;

	if(bind(sock, addr, sizeof(struct sockaddr_in)) == -1){
		mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s", strerror(errno));
		close(sock);
		return -1;
	}

	if(listen(sock, 100) == -1){
		mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s", strerror(errno));
		close(sock);
		return -1;
	}

	return sock;
}

/* Creates a socket and listens on port 'port'.
 * Returns -1 on failure
 * Returns sock number on success.
 */
int mqtt3_socket_listen(uint16_t port)
{
	struct sockaddr_in addr;

	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	return _mqtt3_socket_listen((struct sockaddr *)&addr);
}

/* Creates a socket and listens on port 'port' on address associated with
 * network interface 'iface'.
 * Returns -1 on failure (iface is NULL, socket creation/listen error)
 * Returns sock number on success.
 */
int mqtt3_socket_listen_if(const char *iface, uint16_t port)
{
	struct ifaddrs *ifa;
	struct ifaddrs *tmp;
	int sock;

	if(!iface) return -1;

	if(!getifaddrs(&ifa)){
		tmp = ifa;
		while(tmp){
			if(!strcmp(tmp->ifa_name, iface) && tmp->ifa_addr->sa_family == AF_INET){
				((struct sockaddr_in *)tmp->ifa_addr)->sin_port = htons(port);
				sock = _mqtt3_socket_listen(tmp->ifa_addr);
				freeifaddrs(ifa);
				return sock;
			}
			tmp = tmp->ifa_next;
		}
		freeifaddrs(ifa);
	}
	return -1;
}

int mqtt3_net_packet_queue(mqtt3_context *context, struct _mqtt3_packet *packet)
{
	struct _mqtt3_packet *tail;

	if(!context || !packet) return 1;

	packet->next = NULL;
	if(context->out_packet){
		tail = context->out_packet;
		while(tail->next){
			tail = tail->next;
		}
		tail->next = packet;
	}else{
		context->out_packet = packet;
	}
	return 0;
}

int mqtt3_net_read(mqtt3_context *context)
{
	uint8_t byte;
	ssize_t read_length;
	int rc = 0;

	if(!context || context->sock == -1) return 1;
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
	if(!context->in_packet.command){
		/* FIXME - check command and fill in expected length if we know it.
		 * This means we can check the client is sending valid data some times.
		 */
		read_length = read(context->sock, &byte, 1);
		if(read_length == 1){
			bytes_received++;
			context->in_packet.command = byte;
		}else{
			if(read_length == 0) return 1; /* EOF */
			if(errno == EAGAIN || errno == EWOULDBLOCK){
				return 0;
			}else{
				return 1;
			}
		}
	}
	if(!context->in_packet.have_remaining){
		/* Read remaining
		 * Algorithm for decoding taken from pseudo code at
		 * http://publib.boulder.ibm.com/infocenter/wmbhelp/v6r0m0/topic/com.ibm.etools.mft.doc/ac10870_.htm
		 */
		do{
			read_length = read(context->sock, &byte, 1);
			if(read_length == 1){
				context->in_packet.remaining_count++;
				/* Max 4 bytes length for remaining length as defined by protocol.
				 * Anything more likely means a broken/malicious client.
				 */
				if(context->in_packet.remaining_count > 4) return 1;

				bytes_received++;
				context->in_packet.remaining_length += (byte & 127) * context->in_packet.remaining_mult;
				context->in_packet.remaining_mult *= 128;
			}else{
				if(read_length == 0) return 1; /* EOF */
				if(errno == EAGAIN || errno == EWOULDBLOCK){
					return 0;
				}else{
					return 1;
				}
			}
		}while((byte & 128) != 0);

		if(context->in_packet.remaining_length > 0){
			context->in_packet.payload = mqtt3_malloc(context->in_packet.remaining_length*sizeof(uint8_t));
			if(!context->in_packet.payload) return 1;
			context->in_packet.to_process = context->in_packet.remaining_length;
		}
		context->in_packet.have_remaining = 1;
	}
	while(context->in_packet.to_process>0){
		read_length = read(context->sock, &(context->in_packet.payload[context->in_packet.pos]), context->in_packet.to_process);
		if(read_length > 0){
			bytes_received += read_length;
			context->in_packet.to_process -= read_length;
			context->in_packet.pos += read_length;
		}else{
			if(errno == EAGAIN || errno == EWOULDBLOCK){
				return 0;
			}else{
				return 1;
			}
		}
	}

	msgs_received++;
	/* All data for this packet is read. */
	context->in_packet.pos = 0;
	rc = mqtt3_packet_handle(context);

	/* Free data and reset values */
	mqtt3_context_packet_cleanup(&context->in_packet);

	context->last_msg_in = time(NULL);
	return rc;
}

int mqtt3_net_write(mqtt3_context *context)
{
	uint8_t byte;
	ssize_t write_length;
	struct _mqtt3_packet *packet;

	if(!context || context->sock == -1) return 1;

	while(context->out_packet){
		packet = context->out_packet;

		if(packet->command){
			/* Assign to_proces here before remaining_length changes. */
			packet->to_process = packet->remaining_length;
			packet->pos = 0;

			write_length = write(context->sock, &packet->command, 1);
			if(write_length == 1){
				bytes_sent++;
				packet->command = 0;
			}else{
				if(write_length == 0) return 1; /* EOF */
				if(errno == EAGAIN || errno == EWOULDBLOCK){
					return 0;
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
				write_length = write(context->sock, &byte, 1);
				if(write_length == 1){
					packet->remaining_count++;
					/* Max 4 bytes length for remaining length as defined by protocol. */
					if(packet->remaining_count > 4) return 1;
	
					bytes_sent++;
				}else{
					if(write_length == 0) return 1; /* EOF */
					if(errno == EAGAIN || errno == EWOULDBLOCK){
						return 0;
					}else{
						return 1;
					}
				}
			}while(packet->remaining_length > 0);
			packet->have_remaining = 1;
		}
		while(packet->to_process > 0){
			write_length = write(context->sock, &(packet->payload[packet->pos]), packet->to_process);
			if(write_length > 0){
				bytes_sent += write_length;
				packet->to_process -= write_length;
				packet->pos += write_length;
			}else{
				if(errno == EAGAIN || errno == EWOULDBLOCK){
					return 0;
				}else{
					return 1;
				}
			}
		}

		msgs_sent++;

		/* Free data and reset values */
		context->out_packet = packet->next;
		mqtt3_context_packet_cleanup(packet);
		mqtt3_free(packet);

		context->last_msg_out = time(NULL);
	}
	return 0;
}

int mqtt3_read_byte(mqtt3_context *context, uint8_t *byte)
{
	if(context->in_packet.pos+1 > context->in_packet.remaining_length)
		return 1;

	*byte = context->in_packet.payload[context->in_packet.pos];
	context->in_packet.pos++;

	return 0;
}

int mqtt3_write_byte(struct _mqtt3_packet *packet, uint8_t byte)
{
	if(packet->pos+1 > packet->remaining_length) return 1;

	packet->payload[packet->pos] = byte;
	packet->pos++;

	return 0;
}

int mqtt3_read_bytes(mqtt3_context *context, uint8_t *bytes, uint32_t count)
{
	if(context->in_packet.pos+count > context->in_packet.remaining_length)
		return 1;

	memcpy(bytes, &(context->in_packet.payload[context->in_packet.pos]), count);
	context->in_packet.pos += count;

	return 0;
}

int mqtt3_write_bytes(struct _mqtt3_packet *packet, const uint8_t *bytes, uint32_t count)
{
	if(packet->pos+count > packet->remaining_length) return 1;

	memcpy(&(packet->payload[packet->pos]), bytes, count);
	packet->pos += count;

	return 0;
}

int mqtt3_read_string(mqtt3_context *context, char **str)
{
	uint16_t len;

	if(mqtt3_read_uint16(context, &len)) return 1;

	if(context->in_packet.pos+len > context->in_packet.remaining_length)
		return 1;

	*str = mqtt3_calloc(len+1, sizeof(char));
	if(*str){
		memcpy(*str, &(context->in_packet.payload[context->in_packet.pos]), len);
		context->in_packet.pos += len;
	}else{
		return 1;
	}

	return 0;
}

int mqtt3_write_string(struct _mqtt3_packet *packet, const char *str, uint16_t length)
{
	if(mqtt3_write_uint16(packet, length)) return 1;
	if(mqtt3_write_bytes(packet, (uint8_t *)str, length)) return 1;

	return 0;
}

int mqtt3_read_uint16(mqtt3_context *context, uint16_t *word)
{
	uint8_t msb, lsb;

	if(context->in_packet.pos+2 > context->in_packet.remaining_length)
		return 1;

	msb = context->in_packet.payload[context->in_packet.pos];
	context->in_packet.pos++;
	lsb = context->in_packet.payload[context->in_packet.pos];
	context->in_packet.pos++;

	*word = (msb<<8) + lsb;

	return 0;
}

int mqtt3_write_uint16(struct _mqtt3_packet *packet, uint16_t word)
{
	if(mqtt3_write_byte(packet, MQTT_MSB(word))) return 1;
	if(mqtt3_write_byte(packet, MQTT_LSB(word))) return 1;

	return 0;
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

