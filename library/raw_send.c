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

int mqtt_raw_publish(int sock, bool dup, uint8_t qos, bool retain, const char *topic, uint16_t topiclen, const uint8_t *payload, uint32_t payloadlen)
{
	int packetlen;
	uint16_t mid;

	packetlen = 2+topiclen + payloadlen;
	if(qos > 0) packetlen += 2; /* For message id */

	/* Fixed header */
	if(mqtt_write_byte(sock, PUBLISH | (dup<<3) | (qos<<1) | retain)) return 1;
	if(mqtt_write_remaining_length(sock, packetlen)) return 1;

	/* Variable header (topic string) */
	if(mqtt_write_string(sock, topic, topiclen)) return 1;
	if(qos > 0){
		mid = mqtt_generate_message_id();
		if(mqtt_write_byte(sock, MQTT_MSB(mid))) return 1;
		if(mqtt_write_byte(sock, MQTT_LSB(mid))) return 1;
	}

	/* Payload */
	if(mqtt_write_bytes(sock, payload, payloadlen)) return 1;

	return 0;
}

int mqtt_raw_connect(int sock, const char *client_id, int client_id_len, bool will, uint8_t will_qos, bool will_retain, const char *will_topic, int will_topic_len, const char *will_msg, int will_msg_len, uint16_t keepalive, bool cleanstart)
{
	int payloadlen;

	payloadlen = 2+client_id_len;
	if(will) payloadlen += 2+will_topic_len + 2+will_msg_len;

	/* Fixed header */
	if(mqtt_write_byte(sock, CONNECT)) return 1;
	if(mqtt_write_remaining_length(sock, 12+payloadlen)) return 1;

	/* Variable header */
	if(mqtt_write_string(sock, PROTOCOL_NAME, strlen(PROTOCOL_NAME))) return 1;
	if(mqtt_write_byte(sock, PROTOCOL_VERSION)) return 1;
	if(mqtt_write_byte(sock, (will_retain<<5) | (will_qos<<3) | (will<<2) | (cleanstart<<1))) return 1;
	if(mqtt_write_byte(sock, MQTT_MSB(keepalive))) return 1;
	if(mqtt_write_byte(sock, MQTT_LSB(keepalive))) return 1;

	/* Payload */
	if(mqtt_write_string(sock, client_id, client_id_len)) return 1;
	if(will){
		if(mqtt_write_string(sock, will_topic, will_topic_len)) return 1;
		if(mqtt_write_string(sock, will_msg, will_msg_len)) return 1;
	}

	return 0;
}

int mqtt_send_simple_command(int sock, uint8_t command)
{
	if(mqtt_write_byte(sock, command)) return 1;
	if(mqtt_write_byte(sock, 0)) return 1;

	return 0;
}

int mqtt_raw_pingreq(int sock)
{
	/* FIXME - Update keepalive information or turn into a macro like m_r_disconnect() */
	return mqtt_send_simple_command(sock, PINGREQ);
}

int mqtt_raw_pingresp(int sock)
{
	/* FIXME - Update keepalive information or turn into a macro like m_r_disconnect() */
	return mqtt_send_simple_command(sock, PINGRESP);
}

int mqtt_raw_subscribe(int sock, bool dup, const char *topic, uint16_t topiclen, uint8_t topic_qos)
{
	/* FIXME - only deals with a single topic */
	uint32_t packetlen;
	uint16_t mid;

	packetlen = 2 + 2+topiclen + 1;

	/* Fixed header */
	if(mqtt_write_byte(sock, SUBSCRIBE | (dup<<3) | (1<<1))) return 1;
	if(mqtt_write_remaining_length(sock, packetlen)) return 1;

	/* Variable header */
	mid = mqtt_generate_message_id();
	if(mqtt_write_byte(sock, MQTT_MSB(mid))) return 1;
	if(mqtt_write_byte(sock, MQTT_LSB(mid))) return 1;

	/* Payload */
	if(mqtt_write_string(sock, topic, topiclen)) return 1;
	if(mqtt_write_byte(sock, topic_qos)) return 1;

	return 0;
}


int mqtt_raw_unsubscribe(int sock, bool dup, const char *topic, uint16_t topiclen)
{
	/* FIXME - only deals with a single topic */
	uint32_t packetlen;
	uint16_t mid;

	packetlen = 2 + 2+topiclen;

	/* Fixed header */
	if(mqtt_write_byte(sock, UNSUBSCRIBE | (dup<<3) | (1<<1))) return 1;
	if(mqtt_write_remaining_length(sock, packetlen)) return 1;
	
	/* Variable header */
	mid = mqtt_generate_message_id();
	if(mqtt_write_byte(sock, MQTT_MSB(mid))) return 1;
	if(mqtt_write_byte(sock, MQTT_LSB(mid))) return 1;

	/* Payload */
	if(mqtt_write_string(sock, topic, topiclen)) return 1;

	return 0;
}

