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
	int payloadlen;

	payloadlen = 2+client_id_len;
	if(will) payloadlen += 2+will_topic_len + 2+will_msg_len;

	/* Fixed header */
	mqtt_write_byte(sock, CONNECT);
	mqtt_write_remaining_length(sock, 12+payloadlen);

	/* Variable header */
	mqtt_write_string(sock, PROTOCOL_NAME, strlen(PROTOCOL_NAME));
	mqtt_write_byte(sock, PROTOCOL_VERSION);
	mqtt_write_byte(sock, (will_retain<<5) | (will_qos<<3) | (will<<2) | (cleanstart<<1));
	mqtt_write_byte(sock, MQTT_MSB(keepalive));
	mqtt_write_byte(sock, MQTT_LSB(keepalive));

	/* Payload */
	mqtt_write_string(sock, client_id, client_id_len);
	if(will){
		mqtt_write_string(sock, will_topic, will_topic_len);
		mqtt_write_string(sock, will_msg, will_msg_len);
	}
}

void mqtt_send_simple_command(int sock, uint8_t command)
{
	mqtt_write_byte(sock, command);
	mqtt_write_byte(sock, 0);
}

#define mqtt_raw_disconnect(A) mqtt_send_simple_command(A, DISCONNECT)

void mqtt_raw_pingreq(int sock)
{
	/* FIXME - Update keepalive information or turn into a macro like m_r_disconnect() */
	mqtt_send_simple_command(sock, PINGREQ);
}

void mqtt_raw_pingresp(int sock)
{
	/* FIXME - Update keepalive information or turn into a macro like m_r_disconnect() */
	mqtt_send_simple_command(sock, PINGRESP);
}

void mqtt_raw_subscribe(int sock, bool dup, const char *topic, uint16_t topiclen, char topic_qos)
{
	/* FIXME - only deals with a single topic */
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


void mqtt_raw_unsubscribe(int sock, bool dup, const char *topic, uint16_t topiclen)
{
	/* FIXME - only deals with a single topic */
	uint8_t *packet = NULL;
	int packetlen;
	int pos;
	uint16_t mid;

	/* FIXME - deal with packetlen > 127 */
	packetlen = 2 + 2 + 2+topiclen;

	packet = (uint8_t *)malloc(packetlen);

	/* Fixed header */
	packet[0] = UNSUBSCRIBE | (dup<<3) | (1<<1);
	packet[1] = packetlen - 2; // Remaining bytes
	mid = mqtt_generate_message_id();
	packet[2] = MQTT_MSB(mid);
	packet[3] = MQTT_LSB(mid);
	packet[4] = MQTT_MSB(topiclen);
	packet[5] = MQTT_LSB(topiclen);
	pos = 6;
	memcpy(&(packet[pos]), topic, topiclen);

	write(sock, packet, packetlen);
	free(packet);
}

