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

int mqtt_connect_socket(const char *ip, uint16_t port)
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

uint8_t mqtt_read_byte(int sock)
{
	uint8_t byte;

	read(sock, &byte, 1);

	return byte;
}

int mqtt_write_byte(int sock, uint8_t byte)
{
	if(write(sock, &byte, 1) == 1){
		return 0;
	}else{
		return 1;
	}
}

int mqtt_read_bytes(int sock, uint8_t *bytes, uint32_t count)
{
	if(read(sock, bytes, count) == count){
		return 0;
	}else{
		return 1;
	}
}

int mqtt_write_bytes(int sock, const uint8_t *bytes, uint32_t count)
{
	if(write(sock, bytes, count) == count){
		return 0;
	}else{
		return 1;
	}
}

uint32_t mqtt_read_remaining_length(int sock)
{
	uint32_t value = 0;
	uint32_t multiplier = 1;
	uint8_t digit;

	/* Algorithm for decoding taken from pseudo code at
	 * http://publib.boulder.ibm.com/infocenter/wmbhelp/v6r0m0/topic/com.ibm.etools.mft.doc/ac10870_.htm
	 */
	do{
		digit = mqtt_read_byte(sock);
		value += (digit & 127) * multiplier;
		multiplier *= 128;
	}while((digit & 128) != 0);

	return value;
}

int mqtt_write_remaining_length(int sock, uint32_t length)
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
		if(mqtt_write_byte(sock, digit)) return 1;
	}while(length > 0);

	return 0;
}

uint8_t *mqtt_read_string(int sock)
{
	uint8_t msb, lsb;
	uint16_t len;
	uint8_t *str;

	msb = mqtt_read_byte(sock);
	lsb = mqtt_read_byte(sock);

	len = (msb<<8) + lsb;

	str = calloc(len+1, sizeof(uint8_t));
	if(str){
		if(mqtt_read_bytes(sock, str, len)){
			free(str);
			return NULL;
		}
	}

	return str;
}

int mqtt_write_string(int sock, const char *str, uint16_t length)
{
	if(mqtt_write_byte(sock, MQTT_MSB(length))) return 1;
	if(mqtt_write_byte(sock, MQTT_LSB(length))) return 1;
	if(mqtt_write_bytes(sock, (uint8_t *)str, length)) return 1;

	return 0;
}

