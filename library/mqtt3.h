#ifndef MQTT3_H
#define MQTT3_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

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

/* Data types */
typedef struct _mqtt_message {
	struct _mqtt_message *next;
	time_t timestamp;
	uint16_t message_id;
	uint8_t command;
	uint8_t dup;
	uint8_t qos;
	uint8_t retain;
	uint32_t remaining_length;
	uint8_t *variable_header;
	uint32_t variable_header_len;
	uint8_t *payload;
	uint32_t payload_len;
} mqtt_message;

typedef struct _mqtt_subscription {
	struct _mqtt_subscription *next;
	char *topic;
	uint8_t qos;
} mqtt_subscription;

typedef struct{
	int sock;
	time_t last_message;
	uint16_t keepalive;
	mqtt_message *messages;
	mqtt_subscription *subscriptions;
} mqtt_context;

/* Utility functions */
const char *mqtt_command_to_string(uint8_t command);
uint16_t mqtt_generate_message_id(void);

/* Raw send functions - just construct the packet and send */
int mqtt_raw_connect(mqtt_context *context, const char *client_id, int client_id_len, bool will, uint8_t will_qos, bool will_retain, const char *will_topic, int will_topic_len, const char *will_msg, int will_msg_len, uint16_t keepalive, bool cleanstart);
int mqtt_raw_disconnect(mqtt_context *context);
int mqtt_raw_pingreq(mqtt_context *context);
int mqtt_raw_pingresp(mqtt_context *context);
int mqtt_raw_puback(mqtt_context *context, uint16_t mid);
int mqtt_raw_publish(mqtt_context *context, bool dup, uint8_t qos, bool retain, const char *topic, uint16_t topiclen, const uint8_t *payload, uint32_t payloadlen);
int mqtt_raw_pubrel(mqtt_context *context, uint16_t mid);
int mqtt_raw_subscribe(mqtt_context *context, bool dup, const char *topic, uint16_t topiclen, uint8_t topic_qos);
int mqtt_raw_unsubscribe(mqtt_context *context, bool dup, const char *topic, uint16_t topiclen);
int mqtt_send_simple_command(mqtt_context *context, uint8_t command);

/* Network functions */
int mqtt_connect_socket(const char *ip, uint16_t port);
int mqtt_close_socket(mqtt_context *context);

uint8_t mqtt_read_byte(mqtt_context *context);
int mqtt_read_bytes(mqtt_context *context, uint8_t *bytes, uint32_t count);
uint8_t *mqtt_read_string(mqtt_context *context);
uint32_t mqtt_read_remaining_length(mqtt_context *context);
uint16_t mqtt_read_uint16(mqtt_context *context);

int mqtt_write_byte(mqtt_context *context, uint8_t byte);
int mqtt_write_bytes(mqtt_context *context, const uint8_t *bytes, uint32_t count);
int mqtt_write_string(mqtt_context *context, const char *str, uint16_t length);
int mqtt_write_remaining_length(mqtt_context *context, uint32_t length);
int mqtt_write_uint16(mqtt_context *context, uint16_t word);

/* Message list handling */
int mqtt_add_message(mqtt_context *context, mqtt_message *message);
int mqtt_remove_message(mqtt_context *context, uint16_t mid);
void mqtt_cleanup_message(mqtt_message *message);
void mqtt_cleanup_messages(mqtt_context *context);

/* Managed send functions */
int mqtt_managed_send(mqtt_context *context, mqtt_message *message);
int mqtt_managed_publish(mqtt_context *context, uint8_t qos, bool retain, const char *topic, uint16_t topiclen, const uint8_t *payload, uint32_t payloadlen);

/* Read handling functions */
int mqtt_handle_connack(mqtt_context *context);
int mqtt_handle_puback(mqtt_context *context);
int mqtt_handle_pubcomp(mqtt_context *context);
int mqtt_handle_publish(mqtt_context *context, uint8_t header);
int mqtt_handle_pubrec(mqtt_context *context);
int mqtt_handle_suback(mqtt_context *context);
int mqtt_handle_unsuback(mqtt_context *context);

#endif
