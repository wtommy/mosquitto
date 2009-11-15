#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mqtt3.h>

int mqtt3_close_socket(mqtt3_context *context)
{
	int rc = -1;

	if(!context) return -1;
	if(context->sock != -1){
		mqtt3_db_invalidate_sock(context->id, context->sock);
		rc = close(context->sock);
		context->sock = -1;
	}

	return rc;
}

int mqtt3_connect_socket(const char *ip, uint16_t port)
{
	int sock;
	struct sockaddr_in addr;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == -1){
		fprintf(stderr, "Error: %s\n", strerror(errno));
		return -1;
	}

	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_aton(ip, &(addr.sin_addr));

	if(connect(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) == -1){
		fprintf(stderr, "Error: %s\n", strerror(errno));
		fflush(stderr);
		return -1;
	}

	return sock;
}

int mqtt3_listen_socket(uint16_t port)
{
	int sock;
	struct sockaddr_in addr;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == -1){
		fprintf(stderr, "Error: %s\n", strerror(errno));
		return -1;
	}

	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if(bind(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) == -1){
		fprintf(stderr, "Error: %s\n", strerror(errno));
		fflush(stderr);
		return -1;
	}

	if(listen(sock, 100) == -1){
		fprintf(stderr, "Error: %s\n", strerror(errno));
		fflush(stderr);
		return -1;
	}

	return sock;
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

int mqtt3_read_string(mqtt3_context *context, uint8_t **str)
{
	uint8_t msb, lsb;
	uint16_t len;

	if(mqtt3_read_byte(context, &msb)) return 1;
	if(mqtt3_read_byte(context, &lsb)) return 1;

	len = (msb<<8) + lsb;

	*str = calloc(len+1, sizeof(uint8_t));
	if(*str){
		if(mqtt3_read_bytes(context, *str, len)){
			free(*str);
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

