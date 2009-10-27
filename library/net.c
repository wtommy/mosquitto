#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

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

int mqtt_read_bytes(int sock, uint8_t *bytes, uint32_t count)
{
	if(read(sock, bytes, count) == count){
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

	do{
		digit = mqtt_read_byte(sock);
		value += (digit & 127) * multiplier;
		multiplier *= 128;
	while((digit & 128) != 0);

	return value;
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

