#ifndef MQTT3_H
#define MQTT3_H

#include <stdbool.h>
#include <stdint.h>

/* For version 3 of the MQTT protocol */

#define PROTOCOL_NAME "MQIsdp"
#define PROTOCOL_VERSION 3

/* Macros for accessing the MSB and LSB of a uint16_t */
#define MQTT_MSB(A) (uint8_t)((A & 0xFF00) >> 8)
#define MQTT_LSB(A) (uint8_t)(A & 0x00FF)

/* Message types */
#define CONNECT 0x10
#define CONNACK 0x20
#define PUBLISH 0x30
#define PUBACK 0x40
#define PUBREC 0x50
#define PUBREL 0x60
#define PUBCOMP 0x70
#define SUBSCRIBE 0x80
#define SUBACK 0x90
#define UNSUBSCRIBE 0xA0
#define UNSUBACK 0xB0
#define PINGREQ 0xC0
#define PINGRESP 0xD0
#define DISCONNECT 0xE0

/* Utility functions */
const char *mqtt_command_to_string(uint8_t command);
uint16_t mqtt_generate_message_id(void);

/* Raw send functions - just construct the packet and send */
void mqtt_raw_connect(int sock, const char *client_id, int client_id_len, bool will, uint8_t will_qos, bool will_retain, const char *will_topic, int will_topic_len, const char *will_msg, int will_msg_len, uint16_t keepalive, bool cleanstart);
#define mqtt_raw_disconnect(A) mqtt_send_simple_command(A, DISCONNECT)
int mqtt_raw_pingreq(int sock);
int mqtt_raw_pingresp(int sock);
int mqtt_raw_publish(int sock, bool dup, uint8_t qos, bool retain, const char *topic, uint16_t topiclen, const uint8_t *payload, uint32_t payloadlen);
int mqtt_raw_subscribe(int sock, bool dup, const char *topic, uint16_t topiclen, uint8_t topic_qos);
int mqtt_raw_unsubscribe(int sock, bool dup, const char *topic, uint16_t topiclen);
int mqtt_send_simple_command(int sock, uint8_t command);

/* Network functions */
int mqtt_connect_socket(const char *ip, uint16_t port);

uint8_t mqtt_read_byte(int sock);
int mqtt_read_bytes(int sock, uint8_t *bytes, uint32_t count);
uint8_t *mqtt_read_string(int sock);
uint32_t mqtt_read_remaining_length(int sock);

int mqtt_write_byte(int sock, uint8_t byte);
int mqtt_write_bytes(int sock, const uint8_t *bytes, uint32_t count);
int mqtt_write_string(int sock, const char *str, uint16_t length);
int mqtt_write_remaining_length(int sock, uint32_t length);

#endif
