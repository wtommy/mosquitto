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
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mqtt3.h>

static uint64_t bytes_received = 0;
static uint64_t bytes_sent = 0;

int _mqtt3_socket_listen(struct sockaddr *addr);

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

int mqtt3_net_read(mqtt3_context *context)
{
	uint8_t byte;
	uint32_t read_length;

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
	if(!context->packet.command){
		/* FIXME - check command and fill in expected length if we know it.
		 * This means we can check the client is sending valid data some times.
		 */
		read_length = read(context->sock, &byte, 1);
		if(read_length == 1){
			bytes_received++;
			context->packet.command = byte;
		}else{
			if(read_length == 0) return 1; /* EOF */
			if(errno == EAGAIN || errno == EWOULDBLOCK){
				return 0;
			}else{
				return 1;
			}
		}
	}
	if(!context->packet.have_remaining){
		/* Read remaining
		 * Algorithm for decoding taken from pseudo code at
		 * http://publib.boulder.ibm.com/infocenter/wmbhelp/v6r0m0/topic/com.ibm.etools.mft.doc/ac10870_.htm
		 */
		do{
			read_length = read(context->sock, &byte, 1);
			if(read_length == 1){
				bytes_received++;
				context->packet.remaining_length += (byte & 127) * context->packet.remaining_mult;
				context->packet.remaining_mult *= 128;
			}else{
				if(read_length == 0) return 1; /* EOF */
				if(errno == EAGAIN || errno == EWOULDBLOCK){
					return 0;
				}else{
					return 1;
				}
			}
		}while((byte & 128) != 0);

		if(context->packet.remaining_length > 0){
			context->packet.payload = mqtt3_malloc(context->packet.remaining_length*sizeof(uint8_t));
			if(!context->packet.payload) return 1;
			context->packet.to_read = context->packet.remaining_length;
		}
		context->packet.have_remaining = 1;
	}
	if(context->packet.to_read>0){
		read_length = read(context->sock, &(context->packet.payload[context->packet.pos]), context->packet.to_read);
		if(read_length > 0){
			bytes_received += read_length;
			context->packet.to_read -= read_length;
			context->packet.pos += read_length;
			if(context->packet.to_read == 0){
				/* All data for this packet is read. */
				context->packet.pos = 0;
				mqtt3_packet_handle(context);

				/* Free data and reset values */
				mqtt3_context_packet_cleanup(context);
			}
		}else{
			if(errno == EAGAIN || errno == EWOULDBLOCK){
				return 0;
			}else{
				return 1;
			}
		}
	}
	context->last_msg_in = time(NULL);
	return 0;
}

int mqtt3_read_byte(mqtt3_context *context, uint8_t *byte)
{
	/* FIXME - error checking. */
	*byte = context->packet.payload[context->packet.pos];
	context->packet.pos++;

	return 0;
}

int mqtt3_write_byte(mqtt3_context *context, uint8_t byte)
{
	if(write(context->sock, &byte, 1) == 1){
		bytes_sent++;
		return 0;
	}else{
		return 1;
	}
}

int mqtt3_read_bytes(mqtt3_context *context, uint8_t *bytes, uint32_t count)
{
	/* FIXME - error checking. */
	memcpy(bytes, &(context->packet.payload[context->packet.pos]), count);
	context->packet.pos += count;

	return 0;
}

int mqtt3_write_bytes(mqtt3_context *context, const uint8_t *bytes, uint32_t count)
{
	if(write(context->sock, bytes, count) == count){
		bytes_sent += count;
		return 0;
	}else{
		return 1;
	}
}

int mqtt3_write_remaining_length(mqtt3_context *context, uint32_t length)
{
	uint8_t digit;

	/* Algorithm for encoding taken from pseudo code at
	 * http://publib.boulder.ibm.com/infocenter/wmbhelp/v6r0m0/topic/com.ibm.etools.mft.doc/ac10870_.htm
	 */
	do{
		digit = length % 128;
		length = length / 128;
		/* If there are more digits to encode, set the top bit of this digit */
		if(length>0){
			digit = digit | 0x80;
		}
		if(mqtt3_write_byte(context, digit)) return 1;
	}while(length > 0);

	return 0;
}

int mqtt3_read_string(mqtt3_context *context, char **str)
{
	uint8_t msb, lsb;
	uint16_t len;

	msb = context->packet.payload[context->packet.pos];
	context->packet.pos++;
	lsb = context->packet.payload[context->packet.pos];
	context->packet.pos++;

	len = (msb<<8) + lsb;

	*str = mqtt3_calloc(len+1, sizeof(char));
	if(*str){
		memcpy(*str, &(context->packet.payload[context->packet.pos]), len);
		context->packet.pos += len;
	}else{
		return 1;
	}

	return 0;
}

int mqtt3_write_string(mqtt3_context *context, const char *str, uint16_t length)
{
	if(mqtt3_write_uint16(context, length)) return 1;
	if(mqtt3_write_bytes(context, (uint8_t *)str, length)) return 1;

	return 0;
}

int mqtt3_read_uint16(mqtt3_context *context, uint16_t *word)
{
	uint8_t msb, lsb;

	msb = context->packet.payload[context->packet.pos];
	context->packet.pos++;
	lsb = context->packet.payload[context->packet.pos];
	context->packet.pos++;

	*word = (msb<<8) + lsb;

	return 0;
}

int mqtt3_write_uint16(mqtt3_context *context, uint16_t word)
{
	if(mqtt3_write_byte(context, MQTT_MSB(word))) return 1;
	if(mqtt3_write_byte(context, MQTT_LSB(word))) return 1;

	return 0;
}

uint64_t mqtt3_net_total_bytes_received(void)
{
	return bytes_received;
}

uint64_t mqtt3_net_total_bytes_sent(void)
{
	return bytes_sent;
}

