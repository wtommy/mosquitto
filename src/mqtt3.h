/*
Copyright (c) 2009, Roger Light <roger@atchoo.org>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of mosquitto nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef MQTT3_H
#define MQTT3_H

#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>
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

/* Log destinations */
#define MQTT3_LOG_NONE 0x00
#define MQTT3_LOG_SYSLOG 0x01
#define MQTT3_LOG_FILE 0x02
#define MQTT3_LOG_STDOUT 0x04
#define MQTT3_LOG_STDERR 0x08
#define MQTT3_LOG_TOPIC 0x10

/* Log types */
#define MQTT3_LOG_INFO 0x01
#define MQTT3_LOG_NOTICE 0x02
#define MQTT3_LOG_WARNING 0x04
#define MQTT3_LOG_ERR 0x08
#define MQTT3_LOG_DEBUG 0x10

typedef struct _mqtt3_context{
	int sock;
	time_t last_msg_in;
	time_t last_msg_out;
	uint16_t keepalive;
	bool clean_start;
	char *id;
} mqtt3_context;

typedef enum {
	ms_invalid = 0,
	ms_publish = 1,
	ms_publish_puback = 2,
	ms_wait_puback = 3,
	ms_publish_pubrec = 4,
	ms_wait_pubrec = 5,
	ms_resend_pubrel = 6,
	ms_wait_pubrel = 7,
	ms_resend_pubcomp = 8,
	ms_wait_pubcomp = 9
} mqtt3_msg_status;

typedef enum {
	md_in = 0,
	md_out = 1
} mqtt3_msg_direction;

struct mqtt3_iface {
	char *iface;
	int port;
};

typedef struct {
	int daemon;
	char *ext_sqlite_regex;
	struct mqtt3_iface *iface;
	int iface_count;
	int log_dest;
	int log_type;
	int msg_timeout;
	int persistence;
	char *persistence_location;
	int sys_interval;
	char *pid_file;
	char *user;
} mqtt3_config;

/* ============================================================
 * Utility functions
 * ============================================================ */
/* Return a string that corresponds to the MQTT command number (left shifted 4 bits). */
const char *mqtt3_command_to_string(uint8_t command);

/* ============================================================
 * Config functions
 * ============================================================ */
/* Initialise config struct to default values. */
void mqtt3_config_init(mqtt3_config *config);
/* Parse command line options into config. */
int mqtt3_config_parse_args(mqtt3_config *config, int argc, char *argv[]);
/* Read configuration data from filename into config.
 * Returns 0 on success, 1 if there is a configuration error or if a file cannot be opened.
 */
int mqtt3_config_read(mqtt3_config *config, const char *filename);

/* ============================================================
 * Raw send functions - just construct the packet and send 
 * ============================================================ */
/* Generic function for sending a command to a client where there is no payload, just a mid.
 * Returns 0 on success, 1 on error.
 */
int mqtt3_send_command_with_mid(mqtt3_context *context, uint8_t command, uint16_t mid);
int mqtt3_raw_connack(mqtt3_context *context, uint8_t result);
int mqtt3_raw_connect(mqtt3_context *context, const char *client_id, bool will, uint8_t will_qos, bool will_retain, const char *will_topic, const char *will_msg, uint16_t keepalive, bool clean_start);
int mqtt3_raw_disconnect(mqtt3_context *context);
int mqtt3_raw_pingreq(mqtt3_context *context);
int mqtt3_raw_pingresp(mqtt3_context *context);
int mqtt3_raw_puback(mqtt3_context *context, uint16_t mid);
int mqtt3_raw_pubcomp(mqtt3_context *context, uint16_t mid);
int mqtt3_raw_publish(mqtt3_context *context, bool dup, uint8_t qos, bool retain, uint16_t mid, const char *sub, uint32_t payloadlen, const uint8_t *payload);
int mqtt3_raw_pubrec(mqtt3_context *context, uint16_t mid);
int mqtt3_raw_pubrel(mqtt3_context *context, uint16_t mid);
int mqtt3_raw_subscribe(mqtt3_context *context, bool dup, const char *topic, uint8_t topic_qos);
int mqtt3_raw_unsubscribe(mqtt3_context *context, bool dup, const char *topic);
int mqtt3_send_simple_command(mqtt3_context *context, uint8_t command);

/* ============================================================
 * Network functions
 * ============================================================ */
int mqtt3_socket_connect(const char *ip, uint16_t port);
int mqtt3_socket_close(mqtt3_context *context);
int mqtt3_socket_listen(uint16_t port);
int mqtt3_socket_listen_if(const char *iface, uint16_t port);

int mqtt3_net_read(mqtt3_context *context);
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

/* ============================================================
 * Read handling functions
 * ============================================================ */
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

/* ============================================================
 * Database handling
 * ============================================================ */
int mqtt3_db_open(const char *location, const char *filename, const char *regex_ext_path);
int mqtt3_db_close(void);
int mqtt3_db_client_insert(mqtt3_context *context, int will, int will_retain, int will_qos, const char *will_topic, const char *will_message);
int mqtt3_db_client_update(mqtt3_context *context, int will, int will_retain, int will_qos, const char *will_topic, const char *will_message);
/* Remove the client detailed in context from the clients table only. */
int mqtt3_db_client_delete(mqtt3_context *context);
/* Return the socket number in sock of a client client_id. */
int mqtt3_db_client_find_socket(const char *client_id, int *sock);
/* Set the socket column = -1 for a disconnectd client. */
int mqtt3_db_client_invalidate_socket(const char *client_id, int sock);
/* Add the will of the client in context to the queue of clients subscribed to the appropriate topic. */
int mqtt3_db_client_will_queue(mqtt3_context *context);
/* Return the number of in-flight messages in count. */
int mqtt3_db_message_count(int *count);
int mqtt3_db_message_delete(const char *client_id, uint16_t mid, mqtt3_msg_direction dir);
int mqtt3_db_message_delete_by_oid(uint64_t oid);
int mqtt3_db_message_insert(const char *client_id, uint16_t mid, mqtt3_msg_direction dir, mqtt3_msg_status status, int retain, const char *sub, int qos, uint32_t payloadlen, const uint8_t *payload);
int mqtt3_db_message_release(const char *client_id, uint16_t mid, mqtt3_msg_direction dir);
int mqtt3_db_message_update(const char *client_id, uint16_t mid, mqtt3_msg_direction dir, mqtt3_msg_status status);
int mqtt3_db_message_write(mqtt3_context *context);
int mqtt3_db_messages_delete(const char *client_id);
int mqtt3_db_messages_queue(const char *sub, int qos, uint32_t payloadlen, const uint8_t *payload, int retain);
/* Check all messages waiting on a client reply and resend if timeout has been exceeded. */
int mqtt3_db_message_timeout_check(unsigned int timeout);
/* Generate an outgoing mid for client_id. */
uint16_t mqtt3_db_mid_generate(const char *client_id);
/* Check for clients with pending outgoing messages and add their socks to those being tested as ready to write to in pselect(). */
int mqtt3_db_outgoing_check(fd_set *writefds, int *sockmax);
/* Find retained messages for topic sub and return values in qos, payloadlen and payload. */
int mqtt3_db_retain_find(const char *sub, int *qos, uint32_t *payloadlen, uint8_t **payload);
/* Add a retained message for a subject, overwriting an existing one if necessary. */
int mqtt3_db_retain_insert(const char *sub, int qos, uint32_t payloadlen, const uint8_t *payload);
/* Insert a new subscription/qos for a client. */
int mqtt3_db_sub_insert(const char *client_id, const char *sub, int qos);
/* Remove a subscription for a client. */
int mqtt3_db_sub_delete(const char *client_id, const char *sub);
int mqtt3_db_sub_search_start(const char *sub);
int mqtt3_db_sub_search_next(char **client_id, uint8_t *qos);
/* Remove all subscriptions for a client. */
int mqtt3_db_subs_clean_start(const char *client_id);
void mqtt3_db_sys_update(int interval, time_t start_time);

/* ============================================================
 * Context functions
 * ============================================================ */
mqtt3_context *mqtt3_context_init(int sock);
void mqtt3_context_cleanup(mqtt3_context *context);

/* ============================================================
 * Memory functions
 * ============================================================ */
void *mqtt3_calloc(size_t nmemb, size_t size);
void mqtt3_free(void *mem);
void *mqtt3_malloc(size_t size);
uint32_t mqtt3_memory_used(void);
void *mqtt3_realloc(void *ptr, size_t size);
char *mqtt3_strdup(const char *s);

/* ============================================================
 * Logging functions
 * ============================================================ */
int mqtt3_log_init(int level, int destinations);
int mqtt3_log_close(void);
int mqtt3_log_printf(int level, const char *fmt, ...);

#endif
