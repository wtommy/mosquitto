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
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mqtt3.h>

int _mqtt3_socket_listen(struct sockaddr *addr);

int mqtt3_socket_close(mqtt3_context *context)
{
	int rc = -1;

	if(!context) return -1;
	if(context->sock != -1){
		mqtt3_db_client_invalidate_socket(context->id, context->sock);
		rc = close(context->sock);
		context->sock = -1;
	}

	return rc;
}

int mqtt3_socket_connect(const char *ip, uint16_t port)
{
	int sock;
	struct sockaddr_in addr;

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

int _mqtt3_socket_listen(struct sockaddr *addr)
{
	int sock;
	int opt = 1;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == -1){
		mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s", strerror(errno));
		return -1;
	}

	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

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

int mqtt3_socket_listen(uint16_t port)
{
	struct sockaddr_in addr;

	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	return _mqtt3_socket_listen((struct sockaddr *)&addr);
}

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

int mqtt3_read_byte(mqtt3_context *context, uint8_t *byte)
{
	if(read(context->sock, byte, 1) == 1){
		context->last_msg_in = time(NULL);
		return 0;
	}else{
		return 1;
	}
}

int mqtt3_write_byte(mqtt3_context *context, uint8_t byte)
{
	if(write(context->sock, &byte, 1) == 1){
		return 0;
	}else{
		return 1;
	}
}

int mqtt3_read_bytes(mqtt3_context *context, uint8_t *bytes, uint32_t count)
{
	if(read(context->sock, bytes, count) == count){
		context->last_msg_in = time(NULL);
		return 0;
	}else{
		return 1;
	}
}

int mqtt3_write_bytes(mqtt3_context *context, const uint8_t *bytes, uint32_t count)
{
	if(write(context->sock, bytes, count) == count){
		return 0;
	}else{
		return 1;
	}
}

int mqtt3_read_remaining_length(mqtt3_context *context, uint32_t *remaining)
{
	uint32_t multiplier = 1;
	uint8_t digit;

	/* Algorithm for decoding taken from pseudo code at
	 * http://publib.boulder.ibm.com/infocenter/wmbhelp/v6r0m0/topic/com.ibm.etools.mft.doc/ac10870_.htm
	 */
	(*remaining) = 0;
	do{
		if(mqtt3_read_byte(context, &digit)) return 1;
		(*remaining) += (digit & 127) * multiplier;
		multiplier *= 128;
	}while((digit & 128) != 0);

	return 0;
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

	if(mqtt3_read_byte(context, &msb)) return 1;
	if(mqtt3_read_byte(context, &lsb)) return 1;

	len = (msb<<8) + lsb;

	*str = mqtt3_calloc(len+1, sizeof(char));
	if(*str){
		if(mqtt3_read_bytes(context, (uint8_t *)*str, len)){
			mqtt3_free(*str);
			return 1;
		}
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

	if(mqtt3_read_byte(context, &msb)) return 1;
	if(mqtt3_read_byte(context, &lsb)) return 1;

	*word = (msb<<8) + lsb;

	return 0;
}

int mqtt3_write_uint16(mqtt3_context *context, uint16_t word)
{
	if(mqtt3_write_byte(context, MQTT_MSB(word))) return 1;
	if(mqtt3_write_byte(context, MQTT_LSB(word))) return 1;

	return 0;
}

