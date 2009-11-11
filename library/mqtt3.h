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
typedef struct _mqtt3_message {
	struct _mqtt3_message *next;
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
} mqtt3_message;

typedef struct _mqtt3_subscription {
	struct _mqtt3_subscription *next;
	char *topic;
	uint8_t qos;
} mqtt3_subscription;

typedef struct _mqtt3_context{
	struct _mqtt3_context *next;
	int sock;
	time_t last_message;
	uint16_t keepalive;
	uint8_t *id;
	uint16_t last_mid;
	mqtt3_message *messages;
	mqtt3_subscription *subscriptions;
} mqtt3_context;

/* Utility functions */
const char *mqtt3_command_to_string(uint8_t command);
uint16_t mqtt3_generate_message_id(void);

/* Raw send functions - just construct the packet and send */
int mqtt3_raw_connack(mqtt3_context *context, uint8_t result);
int mqtt3_raw_connect(mqtt3_context *context, const char *client_id, int client_id_len, bool will, uint8_t will_qos, bool will_retain, const char *will_topic, int will_topic_len, const char *will_msg, int will_msg_len, uint16_t keepalive, bool cleanstart);
int mqtt3_raw_disconnect(mqtt3_context *context);
int mqtt3_raw_pingreq(mqtt3_context *context);
int mqtt3_raw_pingresp(mqtt3_context *context);
int mqtt3_raw_puback(mqtt3_context *context, uint16_t mid);
int mqtt3_raw_publish(mqtt3_context *context, bool dup, uint8_t qos, bool retain, const char *topic, uint16_t topiclen, const uint8_t *payload, uint32_t payloadlen);
int mqtt3_raw_pubrel(mqtt3_context *context, uint16_t mid);
int mqtt3_raw_subscribe(mqtt3_context *context, bool dup, const char *topic, uint16_t topiclen, uint8_t topic_qos);
int mqtt3_raw_unsubscribe(mqtt3_context *context, bool dup, const char *topic, uint16_t topiclen);
int mqtt3_send_simple_command(mqtt3_context *context, uint8_t command);

/* Network functions */
int mqtt3_connect_socket(const char *ip, uint16_t port);
int mqtt3_close_socket(mqtt3_context *context);

int mqtt3_read_byte(mqtt3_context *context, uint8_t *byte);
int mqtt3_read_bytes(mqtt3_context *context, uint8_t *bytes, uint32_t count);
int mqtt3_read_string(mqtt3_context *context, uint8_t **str);
int mqtt3_read_remaining_length(mqtt3_context *context, uint32_t *remaining);
int mqtt3_read_uint16(mqtt3_context *context, uint16_t *word);

int mqtt3_write_byte(mqtt3_context *context, uint8_t byte);
int mqtt3_write_bytes(mqtt3_context *context, const uint8_t *bytes, uint32_t count);
int mqtt3_write_string(mqtt3_context *context, const char *str, uint16_t length);
int mqtt3_write_remaining_length(mqtt3_context *context, uint32_t length);
int mqtt3_write_uint16(mqtt3_context *context, uint16_t word);

/* Message list handling */
int mqtt3_add_message(mqtt3_context *context, mqtt3_message *message);
int mqtt3_remove_message(mqtt3_context *context, uint16_t mid);
void mqtt3_cleanup_message(mqtt3_message *message);
void mqtt3_cleanup_messages(mqtt3_context *context);

/* Managed send functions */
int mqtt3_managed_send(mqtt3_context *context, mqtt3_message *message);
int mqtt3_managed_publish(mqtt3_context *context, uint8_t qos, bool retain, const char *topic, uint16_t topiclen, const uint8_t *payload, uint32_t payloadlen);

/* Read handling functions */
int mqtt3_handle_connack(mqtt3_context *context);
int mqtt3_handle_connect(mqtt3_context *context);
int mqtt3_handle_disconnect(mqtt3_context *context);
int mqtt3_handle_pingreq(mqtt3_context *context);
int mqtt3_handle_pingresp(mqtt3_context *context);
int mqtt3_handle_puback(mqtt3_context *context);
int mqtt3_handle_pubcomp(mqtt3_context *context);
int mqtt3_handle_publish(mqtt3_context *context, uint8_t header);
int mqtt3_handle_pubrec(mqtt3_context *context);
int mqtt3_handle_suback(mqtt3_context *context);
int mqtt3_handle_subscribe(mqtt3_context *context);
int mqtt3_handle_unsuback(mqtt3_context *context);
int mqtt3_handle_unsubscribe(mqtt3_context *context);

/* Database handling */
int mqtt3_db_open(const char *filename);
int mqtt3_db_close(void);
int mqtt3_db_insert_client(mqtt3_context *context, int will, int will_retain, int will_qos, int8_t *will_topic, int8_t *will_message);
int mqtt3_db_delete_client(mqtt3_context *context);
int mqtt3_db_insert_sub(mqtt3_context *context, uint8_t *sub, int qos);
int mqtt3_db_delete_sub(mqtt3_context *context, uint8_t *sub);

#endif
