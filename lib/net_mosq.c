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

#include <net_mosq.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

void _mosquitto_packet_cleanup(struct _mosquitto_packet *packet)
{
	if(!packet) return;

	/* Free data and reset values */
	packet->command = 0;
	packet->have_remaining = 0;
	packet->remaining_count = 0;
	packet->remaining_mult = 1;
	packet->remaining_length = 0;
	if(packet->payload) free(packet->payload);
	packet->payload = NULL;
	packet->to_process = 0;
	packet->pos = 0;
}

int _mosquitto_packet_queue(struct mosquitto *mosq, struct _mosquitto_packet *packet)
{
	struct _mosquitto_packet *tail;

	if(!mosq || !packet) return 1;

	packet->next = NULL;
	if(mosq->out_packet){
		tail = mosq->out_packet;
		while(tail->next){
			tail = tail->next;
		}
		tail->next = packet;
	}else{
		mosq->out_packet = packet;
	}
	return 0;
}

/* Close a socket associated with a context and set it to -1.
 * Returns 1 on failure (context is NULL)
 * Returns 0 on success.
 */
int _mosquitto_socket_close(struct mosquitto *mosq)
{
	int rc = 0;

	if(!mosq) return 1;
	if(mosq->sock != -1){
		/* FIXME
		if(mosq->id){
			mqtt3_db_client_invalidate_socket(context->id, context->sock);
		}
		*/
		rc = close(mosq->sock);
		mosq->sock = -1;
	}

	return rc;
}

/* Create a socket and connect it to 'ip' on port 'port'.
 * Returns -1 on failure (ip is NULL, socket creation/connection error)
 * Returns sock number on success.
 */
int _mosquitto_socket_connect(const char *host, uint16_t port)
{
	int sock;
	int opt;
	struct addrinfo hints;
	struct addrinfo *ainfo, *rp;
	int s;

	if(!host || !port) return -1;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET; /* IPv4 only at the moment. */
	hints.ai_socktype = SOCK_STREAM;

	s = getaddrinfo(host, NULL, &hints, &ainfo);
	if(s) return -1;

	for(rp = ainfo; rp != NULL; rp = rp->ai_next){
		sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if(sock == -1) continue;
		
		((struct sockaddr_in *)rp->ai_addr)->sin_port = htons(port);
		if(connect(sock, rp->ai_addr, rp->ai_addrlen) != -1){
			break;
		}

		return -1;
	}
	if(!rp){
		fprintf(stderr, "Error: %s", strerror(errno));
		return -1;
	}
	freeaddrinfo(ainfo);

	/* Set non-blocking */
	opt = fcntl(sock, F_GETFL, 0);
	if(opt == -1 || fcntl(sock, F_SETFL, opt | O_NONBLOCK) == -1){
		close(sock);
		return -1;
	}

	return sock;
}

int _mosquitto_read_byte(struct _mosquitto_packet *packet, uint8_t *byte)
{
	if(packet->pos+1 > packet->remaining_length)
		return 1;

	*byte = packet->payload[packet->pos];
	packet->pos++;

	return 0;
}

int _mosquitto_write_byte(struct _mosquitto_packet *packet, uint8_t byte)
{
	if(packet->pos+1 > packet->remaining_length) return 1;

	packet->payload[packet->pos] = byte;
	packet->pos++;

	return 0;
}

int _mosquitto_read_bytes(struct _mosquitto_packet *packet, uint8_t *bytes, uint32_t count)
{
	if(packet->pos+count > packet->remaining_length)
		return 1;

	memcpy(bytes, &(packet->payload[packet->pos]), count);
	packet->pos += count;

	return 0;
}

int _mosquitto_write_bytes(struct _mosquitto_packet *packet, const uint8_t *bytes, uint32_t count)
{
	if(packet->pos+count > packet->remaining_length) return 1;

	memcpy(&(packet->payload[packet->pos]), bytes, count);
	packet->pos += count;

	return 0;
}

int _mosquitto_read_string(struct _mosquitto_packet *packet, char **str)
{
	uint16_t len;

	if(_mosquitto_read_uint16(packet, &len)) return 1;

	if(packet->pos+len > packet->remaining_length)
		return 1;

	*str = calloc(len+1, sizeof(char));
	if(*str){
		memcpy(*str, &(packet->payload[packet->pos]), len);
		packet->pos += len;
	}else{
		return 1;
	}

	return 0;
}

int _mosquitto_write_string(struct _mosquitto_packet *packet, const char *str, uint16_t length)
{
	if(_mosquitto_write_uint16(packet, length)) return 1;
	if(_mosquitto_write_bytes(packet, (uint8_t *)str, length)) return 1;

	return 0;
}

int _mosquitto_read_uint16(struct _mosquitto_packet *packet, uint16_t *word)
{
	uint8_t msb, lsb;

	if(packet->pos+2 > packet->remaining_length)
		return 1;

	msb = packet->payload[packet->pos];
	packet->pos++;
	lsb = packet->payload[packet->pos];
	packet->pos++;

	*word = (msb<<8) + lsb;

	return 0;
}

int _mosquitto_write_uint16(struct _mosquitto_packet *packet, uint16_t word)
{
	if(_mosquitto_write_byte(packet, MOSQ_MSB(word))) return 1;
	if(_mosquitto_write_byte(packet, MOSQ_LSB(word))) return 1;

	return 0;
}

