#ifndef MQTT3_H
#define MQTT3_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/select.h>
#include <time.h>

/* For version 3 of the MQTT protocol */

#define PROTOCOL_NAME "MQIsdp"
#define PROTOCOL_VERSION 3

/* Database macros */
#define MQTT_DB_VERSION 0

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

typedef struct _mqtt3_context{
	struct _mqtt3_context *next;
	int sock;
	time_t last_msg_in;
	time_t last_msg_out;
	uint16_t keepalive;
	bool clean_start;
	char *id;
	mqtt3_message *messages;
} mqtt3_context;

typedef enum {
	ms_invalid = 0,
	ms_publish = 1,
	ms_wait_puback = 2,
	ms_wait_pubrec = 3,
	ms_wait_pubrel = 4,
	ms_wait_pubcomp = 5
} mqtt3_msg_status;

typedef enum {
	md_in = 0,
	md_out = 1
} mqtt3_msg_direction;

/* Utility functions */
const char *mqtt3_command_to_string(uint8_t command);

/* Raw send functions - just construct the packet and send */
int mqtt3_send_command_with_mid(mqtt3_context *context, uint8_t command, uint16_t mid);
int mqtt3_raw_connack(mqtt3_context *context, uint8_t result);
int mqtt3_raw_connect(mqtt3_context *context, const char *client_id, int client_id_len, bool will, uint8_t will_qos, bool will_retain, const char *will_topic, int will_topic_len, const char *will_msg, int will_msg_len, uint16_t keepalive, bool clean_start);
int mqtt3_raw_disconnect(mqtt3_context *context);
int mqtt3_raw_pingreq(mqtt3_context *context);
int mqtt3_raw_pingresp(mqtt3_context *context);
int mqtt3_raw_puback(mqtt3_context *context, uint16_t mid);
int mqtt3_raw_pubcomp(mqtt3_context *context, uint16_t mid);
int mqtt3_raw_publish(mqtt3_context *context, bool dup, uint8_t qos, bool retain, uint16_t mid, const char *sub, uint32_t payloadlen, const uint8_t *payload);
int mqtt3_raw_pubrec(mqtt3_context *context, uint16_t mid);
int mqtt3_raw_pubrel(mqtt3_context *context, uint16_t mid);
int mqtt3_raw_subscribe(mqtt3_context *context, bool dup, const char *topic, uint16_t topiclen, uint8_t topic_qos);
int mqtt3_raw_unsubscribe(mqtt3_context *context, bool dup, const char *topic, uint16_t topiclen);
int mqtt3_send_simple_command(mqtt3_context *context, uint8_t command);

/* Network functions */
int mqtt3_socket_connect(const char *ip, uint16_t port);
int mqtt3_socket_close(mqtt3_context *context);
int mqtt3_socket_listen(uint16_t port);

int mqtt3_read_byte(mqtt3_context *context, uint8_t *byte);
int mqtt3_read_bytes(mqtt3_context *context, uint8_t *bytes, uint32_t count);
int mqtt3_read_string(mqtt3_context *context, char **str);
int mqtt3_read_remaining_length(mqtt3_context *context, uint32_t *remaining);
int mqtt3_read_uint16(mqtt3_context *context, uint16_t *word);

int mqtt3_write_byte(mqtt3_context *context, uint8_t byte);
int mqtt3_write_bytes(mqtt3_context *context, const uint8_t *bytes, uint32_t count);
int mqtt3_write_string(mqtt3_context *context, const char *str, uint16_t length);
int mqtt3_write_remaining_length(mqtt3_context *context, uint32_t length);
int mqtt3_write_uint16(mqtt3_context *context, uint16_t word);

/* Message list handling */
int mqtt3_message_add(mqtt3_context *context, mqtt3_message *message);
int mqtt3_message_remove(mqtt3_context *context, uint16_t mid);
void mqtt3_message_cleanup(mqtt3_message *message);
void mqtt3_messages_cleanup(mqtt3_context *context);

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
int mqtt3_handle_pubrel(mqtt3_context *context);
int mqtt3_handle_suback(mqtt3_context *context);
int mqtt3_handle_subscribe(mqtt3_context *context);
int mqtt3_handle_unsuback(mqtt3_context *context);
int mqtt3_handle_unsubscribe(mqtt3_context *context);

/* Database handling */
int mqtt3_db_open(const char *filename);
int mqtt3_db_close(void);
int mqtt3_db_client_insert(mqtt3_context *context, int will, int will_retain, int will_qos, const char *will_topic, const char *will_message);
int mqtt3_db_client_update(mqtt3_context *context, int will, int will_retain, int will_qos, const char *will_topic, const char *will_message);
int mqtt3_db_client_delete(mqtt3_context *context);
int mqtt3_db_client_find_socket(const char *client_id, int *sock);
int mqtt3_db_client_invalidate_socket(const char *client_id, int sock);
int mqtt3_db_message_delete(const char *client_id, uint16_t mid);
int mqtt3_db_message_delete_by_oid(uint64_t oid);
int mqtt3_db_message_insert(const char *client_id, uint16_t mid, mqtt3_msg_direction dir, mqtt3_msg_status status, const char *sub, int qos, uint32_t payloadlen, uint8_t *payload);
int mqtt3_db_message_update(const char *client_id, uint16_t mid, mqtt3_msg_direction dir, mqtt3_msg_status status);
int mqtt3_db_message_write(mqtt3_context *context);
int mqtt3_db_messages_delete(const char *client_id);
int mqtt3_db_messages_queue(const char *sub, int qos, uint32_t payloadlen, uint8_t *payload, int retain);
uint16_t mqtt3_db_mid_generate(const char *client_id);
int mqtt3_db_outgoing_check(fd_set *writefds, int *sockmax);
int mqtt3_db_retain_find(const char *sub, int *qos, uint32_t *payloadlen, uint8_t **payload);
int mqtt3_db_retain_insert(const char *sub, int qos, uint32_t payloadlen, uint8_t *payload);
int mqtt3_db_sub_insert(const char *client_id, const char *sub, int qos);
int mqtt3_db_sub_delete(const char *client_id, const char *sub);
int mqtt3_db_sub_search_start(const char *sub);
int mqtt3_db_sub_search_next(char **client_id, uint8_t *qos);
int mqtt3_db_subs_clean_start(const char *client_id);

/* Context functions */
mqtt3_context *mqtt3_context_init(int sock);
void mqtt3_context_cleanup(mqtt3_context *context);

/* Memory functions */
void *mqtt3_calloc(size_t nmemb, size_t size);
void mqtt3_free(void *mem);
void *mqtt3_malloc(size_t size);
uint32_t mqtt3_memory_used(void);
void *mqtt3_realloc(void *ptr, size_t size);
char *mqtt3_strdup(const char *s);

#endif
