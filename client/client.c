#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#define CONNECT 0x10
#define PUBLISH 0x30

#define MQTT_MSB(A) (uint8_t)((A & 0xFF00) >> 8)
#define MQTT_LSB(A) (uint8_t)(A & 0x00FF)

uint16_t mqtt_generate_message_id(void)
{
	return 1;
}

uint16_t mqtt_raw_publish(int sock, bool dup, uint8_t qos, bool retain, const char *topic, uint16_t topiclen, const char *payload, int payloadlen)
{
	uint8_t *packet = NULL;
	int packetlen, bytes;
	int pos;
	uint16_t mid;

	packetlen = 2+2+topiclen + payloadlen;
	if(qos > 0) packetlen += 2; /* For message id */

	packet = (uint8_t *)malloc(packetlen);

	/* Fixed header */
	packet[0] = PUBLISH | (dup<<3) | (qos<<2) | retain;
	packet[1] = packetlen-2; /* Don't include fixed header length */

	/* Variable header (topic string) */
	packet[2] = MQTT_MSB(topiclen);
	packet[3] = MQTT_LSB(topiclen);
	memcpy(&(packet[4]), topic, topiclen);
	pos = 4+topiclen;
	if(qos > 0){
		mid = mqtt_generate_message_id();
		packet[pos] = MQTT_MSB(mid);
		pos++;
		packet[pos] = MQTT_LSB(mid);
		pos++;
	}
	memcpy(&(packet[pos]), payload, payloadlen);
	bytes = write(sock, packet, packetlen);
	return bytes-packetlen;
}

int main(int argc, char *argv[])
{
	int sock;
	struct sockaddr_in addr;
	char buf;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == -1){
		fprintf(stderr, "Error: %s\n", strerror(errno));
		return 1;
	}

	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(1883);
	inet_aton("127.0.0.1", &(addr.sin_addr));

	if(connect(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) == -1){
		fprintf(stderr, "Error: %s\n", strerror(errno));
		fflush(stderr);
		return 1;
	}

	buf = 16; write(sock, &buf, 1); // CONNECT
	buf = 19; write(sock, &buf, 1); // Remaining length
	buf = 0; write(sock, &buf, 1); // UTF-8 length MSB
	buf = 6; write(sock, &buf, 1); // UTF-8 length LSB
	buf = 'M'; write(sock, &buf, 1);
	buf = 'Q'; write(sock, &buf, 1);
	buf = 'I'; write(sock, &buf, 1);
	buf = 's'; write(sock, &buf, 1);
	buf = 'd'; write(sock, &buf, 1);
	buf = 'p'; write(sock, &buf, 1);
	buf = 3; write(sock, &buf, 1); // Protocol version
	buf = 2; write(sock, &buf, 1); // Clean start, no Will
	buf = 0; write(sock, &buf, 1); // Keep alive MSB 
	buf = 10; write(sock, &buf, 1); // Keep alive LSB
	buf = 0; write(sock, &buf, 1); // UTF-8 length MSB
	buf = 5; write(sock, &buf, 1); // UTF-8 length LSB
	buf = 'R'; write(sock, &buf, 1);
	buf = 'o'; write(sock, &buf, 1);
	buf = 'g'; write(sock, &buf, 1);
	buf = 'e'; write(sock, &buf, 1);
	buf = 'r'; write(sock, &buf, 1);

	read(sock, &buf, 1);
	if(buf == 32){
		// CONNACK
		read(sock, &buf, 1);//Reserved
		read(sock, &buf, 1); // Return code
		printf("%d\n", buf);

		if(buf == 0){
			// Connection accepted
			mqtt_raw_publish(sock, false, 0, false, "a/b/c", 5, "Roger", 5);
			read(sock, &buf, 1);
			printf("%d\n", buf);
			read(sock, &buf, 1);
			printf("%d\n", buf);
			read(sock, &buf, 1);
			printf("%d\n", buf);
			read(sock, &buf, 1);
			printf("%d\n", buf);
		}
	}
	sleep(2);
	close(sock);

	return 0;
}

