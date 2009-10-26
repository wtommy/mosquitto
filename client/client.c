#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

	/* FIXME - deal with packetlen > 127 */
	packetlen = 2+2+topiclen + payloadlen;
	if(qos > 0) packetlen += 2; /* For message id */

	packet = (uint8_t *)malloc(packetlen);

	/* Fixed header */
	packet[0] = PUBLISH | (dup<<3) | (qos<<1) | retain;
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
	free(packet);
	return bytes-packetlen;
}

void mqtt_raw_connect(int sock, const char *client_id, int client_id_len, bool will, uint8_t will_qos, bool will_retain, const char *will_topic, int will_topic_len, const char *will_msg, int will_msg_len, uint16_t keepalive, bool cleanstart)
{
	uint8_t *packet = NULL;
	int packetlen;
	int pos;

	/* FIXME - deal with packetlen > 127 */
	packetlen = 2 + 12 + 2+client_id_len;
	if(will) packetlen += 2+will_topic_len + 2+will_msg_len;

	packet = (uint8_t *)malloc(packetlen);

	/* Fixed header */
	packet[0] = CONNECT;
	packet[1] = packetlen - 2; // Remaining bytes

	/* Variable header */
	packet[2] = 0; // Protocol name UTF-8 MSB
	packet[3] = 6; // Protocol name UTF-8 LSB
	sprintf(&(packet[4]), "MQIsdp"); // Protocol name
	packet[10] = 3; // Protocol version
	packet[11] = (will_retain<<5) | (will_qos<<3) | (will<<2) | (cleanstart<<1);
	packet[12] = MQTT_MSB(keepalive);
	packet[13] = MQTT_LSB(keepalive);

	/* Payload */
	packet[14] = MQTT_MSB(client_id_len);
	packet[15] = MQTT_LSB(client_id_len);
	memcpy(&(packet[16]), client_id, client_id_len);
	pos = 16+client_id_len;
	if(will){
		packet[pos] = MQTT_MSB(will_topic_len);
		pos++;
		packet[pos] = MQTT_LSB(will_topic_len);
		pos++;
		memcpy(&(packet[pos]), will_topic, will_topic_len);
		pos+=will_topic_len;
		packet[pos] = MQTT_MSB(will_msg_len);
		pos++;
		packet[pos] = MQTT_LSB(will_msg_len);
		pos++;
		memcpy(&(packet[pos]), will_msg, will_msg_len);
	}
	write(sock, packet, packetlen);
	free(packet);
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

	mqtt_raw_connect(sock, "Roger", 5, false, 0, false, "", 0, "", 0, 10, false);

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

