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

#include <mqtt3.h>

uint16_t mqtt_generate_message_id(void)
{
	static uint16_t mid = 123;

	return ++mid;
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

void mqtt_raw_disconnect(int sock)
{
	uint8_t packet[2];

	packet[0] = DISCONNECT;
	packet[1] = 0;

	write(sock, packet, 2);
}

void mqtt_raw_pingreq(int sock)
{
	uint8_t packet[2];

	packet[0] = PINGREQ;
	packet[1] = 0;

	write(sock, packet, 2);
}

void mqtt_raw_subscribe(int sock, bool dup, const char *topic, uint16_t topiclen, char topic_qos)
{
	uint8_t *packet = NULL;
	int packetlen;
	int pos;
	uint16_t mid;

	/* FIXME - deal with packetlen > 127 */
	packetlen = 2 + 2 + 2+topiclen + 1;

	packet = (uint8_t *)malloc(packetlen);

	/* Fixed header */
	packet[0] = SUBSCRIBE | (dup<<3) | (1<<1);
	packet[1] = packetlen - 2; // Remaining bytes
	mid = mqtt_generate_message_id();
	packet[2] = MQTT_MSB(mid);
	packet[3] = MQTT_LSB(mid);
	packet[4] = MQTT_MSB(topiclen);
	packet[5] = MQTT_LSB(topiclen);
	pos = 6;
	memcpy(&(packet[pos]), topic, topiclen);
	pos += topiclen;
	packet[pos] = topic_qos;

	write(sock, packet, packetlen);
	free(packet);
}


int main(int argc, char *argv[])
{
	int sock;
	struct sockaddr_in addr;
	unsigned char buf;

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
	printf("%s ", mqtt_command_to_string(buf&0xF0));
	if(buf == 32){
		// CONNACK
		read(sock, &buf, 1); // Remaining length
		printf("%d ", buf);
		read(sock, &buf, 1);//Reserved
		printf("%d ", buf);
		read(sock, &buf, 1); // Return code
		printf("%d\n", buf);

		if(buf == 0){
			// Connection accepted
			mqtt_raw_subscribe(sock, false, "a/b/c", 5, 0);
			read(sock, &buf, 1);
			printf("%s ", mqtt_command_to_string(buf&0xF0));
			read(sock, &buf, 1); // Remaining length
			printf("%d ", buf);
			read(sock, &buf, 1); // Message ID MSB
			printf("%d ", buf);
			read(sock, &buf, 1); // Message ID LSB
			printf("%d ", buf);
			read(sock, &buf, 1); // Granted QoS
			printf("%d\n", buf);


			mqtt_raw_publish(sock, false, 0, false, "a/b/c", 5, "Roger", 5);
			read(sock, &buf, 1); // Should be 00110000 = 48 = PUBLISH
			printf("%s ", mqtt_command_to_string(buf&0xF0));
			read(sock, &buf, 1); // Remaining length (should be 12)
			printf("%d ", buf);
			read(sock, &buf, 1); // Topic MSB
			printf("%d ", buf);
			read(sock, &buf, 1); // Topic LSB
			printf("%d ", buf);
			read(sock, &buf, 1); // Topic
			printf("%c", buf);
			read(sock, &buf, 1);
			printf("%c", buf);
			read(sock, &buf, 1);
			printf("%c", buf);
			read(sock, &buf, 1);
			printf("%c", buf);
			read(sock, &buf, 1);
			printf("%c ", buf);
			read(sock, &buf, 1); // Payload
			printf("%c", buf);
			read(sock, &buf, 1);
			printf("%c", buf);
			read(sock, &buf, 1);
			printf("%c", buf);
			read(sock, &buf, 1);
			printf("%c", buf);
			read(sock, &buf, 1);
			printf("%c\n", buf);
		}
	}

	mqtt_raw_pingreq(sock);
	read(sock, &buf, 1);
	printf("%s ", mqtt_command_to_string(buf&0xF0));
	read(sock, &buf, 1);
	printf("%d\n", buf);

	sleep(2);
	mqtt_raw_disconnect(sock);
	close(sock);

	return 0;
}

