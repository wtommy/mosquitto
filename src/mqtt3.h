/*
Copyright (c) 2009-2011 Roger Light <roger@atchoo.org>
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

#ifndef CMAKE
#include <config.h>
#endif

#include <net_mosq.h>

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifndef __GNUC__
#define __attribute__(attrib)
#endif

/* Database macros */
#define MOSQ_DB_VERSION 1

/* Log destinations */
#define MQTT3_LOG_NONE 0x00
#define MQTT3_LOG_SYSLOG 0x01
#define MQTT3_LOG_FILE 0x02
#define MQTT3_LOG_STDOUT 0x04
#define MQTT3_LOG_STDERR 0x08
#define MQTT3_LOG_TOPIC 0x10
#define MQTT3_LOG_ALL 0xFF

#define MOSQ_ACL_NONE 0x00
#define MOSQ_ACL_READ 0x01
#define MOSQ_ACL_WRITE 0x02

typedef uint64_t dbid_t;

enum mqtt3_msg_state {
	ms_invalid = 0,
	ms_publish = 1,
	ms_publish_puback = 2,
	ms_wait_puback = 3,
	ms_publish_pubrec = 4,
	ms_wait_pubrec = 5,
	ms_resend_pubrel = 6,
	ms_wait_pubrel = 7,
	ms_resend_pubcomp = 8,
	ms_wait_pubcomp = 9,
	ms_resend_pubrec = 10,
	ms_queued = 11
};

struct _mqtt3_listener {
	int fd;
	char *host;
	uint16_t port;
	int max_connections;
	char *mount_point;
};

typedef struct {
	char *acl_file;
	bool allow_anonymous;
	int autosave_interval;
	char *clientid_prefixes;
	bool daemon;
	struct _mqtt3_listener default_listener;
	struct _mqtt3_listener *listeners;
	int listener_count;
	int log_dest;
	int log_type;
	int max_connections;
	char *password_file;
	bool persistence;
	char *persistence_location;
	char *persistence_file;
	int retry_interval;
	int store_clean_interval;
	int sys_interval;
	char *pid_file;
	char *user;
	struct _mqtt3_bridge *bridges;
	int bridge_count;
} mqtt3_config;

struct _mosquitto_subleaf {
	struct _mosquitto_subleaf *prev;
	struct _mosquitto_subleaf *next;
	struct _mqtt3_context *context;
	int qos;
};

struct _mosquitto_subhier {
	struct _mosquitto_subhier *children;
	struct _mosquitto_subhier *next;
	struct _mosquitto_subleaf *subs;
	char *topic;
	struct mosquitto_msg_store *retained;
};

struct mosquitto_msg_store{
	struct mosquitto_msg_store *next;
	dbid_t db_id;
	int ref_count;
	char *source_id;
	uint16_t source_mid;
	struct mosquitto_message msg;
};

typedef struct _mosquitto_client_msg{
	struct _mosquitto_client_msg *next;
	struct mosquitto_msg_store *store;
	uint16_t mid;
	int qos;
	bool retain;
	time_t timestamp;
	enum mosquitto_msg_direction direction;
	enum mqtt3_msg_state state;
	bool dup;
} mosquitto_client_msg;

struct _mosquitto_unpwd{
	struct _mosquitto_unpwd *next;
	char *username;
	char *password;
};

struct _mosquitto_acl{
	struct _mosquitto_acl *child;
	struct _mosquitto_acl *next;
	char *topic;
	int access;
};

struct _mosquitto_acl_user{
	struct _mosquitto_acl_user *next;
	char *username;
	struct _mosquitto_acl *acl;
};

typedef struct _mosquitto_db{
	dbid_t last_db_id;
	struct _mosquitto_subhier subs;
	struct _mosquitto_unpwd *unpwd;
	struct _mosquitto_acl_user *acl_list;
	struct _mqtt3_context **contexts;
	int context_count;
	struct mosquitto_msg_store *msg_store;
	int msg_store_count;
	char *filepath;
	mqtt3_config *config;
} mosquitto_db;

enum mqtt3_bridge_direction{
	bd_out = 0,
	bd_in = 1,
	bd_both = 2
};

struct _mqtt3_bridge_topic{
	char *topic;
	enum mqtt3_bridge_direction direction;
};

struct _mqtt3_bridge{
	char *name;
	char *address;
	char *clientid;
	uint16_t port;
	int keepalive;
	bool clean_session;
	struct _mqtt3_bridge_topic *topics;
	int topic_count;
	time_t restart_t;
	char *username;
	char *password;
};

typedef struct _mqtt3_context{
	struct _mosquitto_core core;
	bool duplicate;
	struct _mqtt3_bridge *bridge;
	mosquitto_client_msg *msgs;
	struct _mosquitto_acl_user *acl_list;
} mqtt3_context;

/* ============================================================
 * Utility functions
 * ============================================================ */
/* Return a string that corresponds to the MQTT command number (left shifted 4 bits). */
const char *mqtt3_command_to_string(uint8_t command);
void mqtt3_check_keepalive(mqtt3_context *context);

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
int mqtt3_send_command_with_mid(mqtt3_context *context, uint8_t command, uint16_t mid, bool dup);
int mqtt3_raw_connack(mqtt3_context *context, uint8_t result);
int mqtt3_raw_connect(mqtt3_context *context, const char *client_id, bool will, uint8_t will_qos, bool will_retain, const char *will_topic, const char *will_msg, uint16_t keepalive, bool clean_session);
int mqtt3_raw_disconnect(mqtt3_context *context);
int mqtt3_raw_pingreq(mqtt3_context *context);
int mqtt3_raw_pingresp(mqtt3_context *context);
int mqtt3_raw_puback(mqtt3_context *context, uint16_t mid);
int mqtt3_raw_pubcomp(mqtt3_context *context, uint16_t mid);
int mqtt3_raw_publish(mqtt3_context *context, int dup, uint8_t qos, bool retain, uint16_t mid, const char *topic, uint32_t payloadlen, const uint8_t *payload);
int mqtt3_raw_pubrec(mqtt3_context *context, uint16_t mid);
int mqtt3_raw_pubrel(mqtt3_context *context, uint16_t mid, bool dup);
int mqtt3_raw_suback(mqtt3_context *context, uint16_t mid, uint32_t payloadlen, const uint8_t *payload);
int mqtt3_raw_subscribe(mqtt3_context *context, bool dup, const char *topic, uint8_t topic_qos);
int mqtt3_raw_unsubscribe(mqtt3_context *context, bool dup, const char *topic);
int mqtt3_send_simple_command(mqtt3_context *context, uint8_t command);

/* ============================================================
 * Network functions
 * ============================================================ */
int mqtt3_socket_accept(mqtt3_context ***contexts, int *context_count, int listensock);
void mqtt3_socket_close(mqtt3_context *context);
int mqtt3_socket_listen(const char *host, uint16_t port, int **socks, int *sock_count);

int mqtt3_net_packet_queue(mqtt3_context *context, struct _mosquitto_packet *packet);
int mqtt3_net_read(mosquitto_db *db, mqtt3_context *context);
int mqtt3_net_write(mqtt3_context *context);

void mqtt3_net_set_max_connections(int max);
uint64_t mqtt3_net_bytes_total_received(void);
uint64_t mqtt3_net_bytes_total_sent(void);
unsigned long mqtt3_net_msgs_total_received(void);
unsigned long mqtt3_net_msgs_total_sent(void);

/* ============================================================
 * Read handling functions
 * ============================================================ */
int mqtt3_packet_handle(mosquitto_db *db, mqtt3_context *context);
int mqtt3_handle_connack(mqtt3_context *context);
int mqtt3_handle_connect(mosquitto_db *db, mqtt3_context *context);
int mqtt3_handle_disconnect(mqtt3_context *context);
int mqtt3_handle_pingreq(mqtt3_context *context);
int mqtt3_handle_pingresp(mqtt3_context *context);
int mqtt3_handle_puback(mqtt3_context *context);
int mqtt3_handle_pubcomp(mqtt3_context *context);
int mqtt3_handle_publish(mosquitto_db *db, mqtt3_context *context);
int mqtt3_handle_pubrec(mqtt3_context *context);
int mqtt3_handle_pubrel(mosquitto_db *db, mqtt3_context *context);
int mqtt3_handle_suback(mqtt3_context *context);
int mqtt3_handle_subscribe(mosquitto_db *db, mqtt3_context *context);
int mqtt3_handle_unsuback(mqtt3_context *context);
int mqtt3_handle_unsubscribe(mosquitto_db *db, mqtt3_context *context);

/* ============================================================
 * Database handling
 * ============================================================ */
int mqtt3_db_open(mqtt3_config *config, mosquitto_db *db);
int mqtt3_db_close(mosquitto_db *db);
#ifdef WITH_PERSISTENCE
int mqtt3_db_backup(mosquitto_db *db, bool cleanup, bool shutdown);
int mqtt3_db_restore(mosquitto_db *db);
#endif
int mqtt3_db_client_count(mosquitto_db *db, int *count);
/* Add the will of the client in context to the queue of clients subscribed to the appropriate topic. */
int mqtt3_db_client_will_queue(mosquitto_db *db, mqtt3_context *context);
void mqtt3_db_limits_set(int inflight, int queued);
/* Return the number of in-flight messages in count. */
int mqtt3_db_message_count(int *count);
int mqtt3_db_message_delete(mqtt3_context *context, uint16_t mid, enum mosquitto_msg_direction dir);
int mqtt3_db_message_insert(mqtt3_context *context, uint16_t mid, enum mosquitto_msg_direction dir, int qos, bool retain, struct mosquitto_msg_store *stored);
int mqtt3_db_message_release(mosquitto_db *db, mqtt3_context *context, uint16_t mid, enum mosquitto_msg_direction dir);
int mqtt3_db_message_update(mqtt3_context *context, uint16_t mid, enum mosquitto_msg_direction dir, enum mqtt3_msg_state state);
int mqtt3_db_message_write(mqtt3_context *context);
int mqtt3_db_messages_delete(mqtt3_context *context);
int mqtt3_db_messages_easy_queue(mosquitto_db *db, mqtt3_context *context, const char *topic, int qos, uint32_t payloadlen, const uint8_t *payload, int retain);
int mqtt3_db_messages_queue(mosquitto_db *db, const char *source_id, const char *topic, int qos, int retain, struct mosquitto_msg_store *stored);
int mqtt3_db_message_store(mosquitto_db *db, const char *source, uint16_t source_mid, const char *topic, int qos, uint32_t payloadlen, const uint8_t *payload, int retain, struct mosquitto_msg_store **stored, dbid_t store_id);
int mqtt3_db_message_store_find(mosquitto_db *db, const char *source, uint16_t mid, struct mosquitto_msg_store **stored);
/* Check all messages waiting on a client reply and resend if timeout has been exceeded. */
int mqtt3_db_message_timeout_check(mosquitto_db *db, unsigned int timeout);
int mqtt3_retain_queue(mosquitto_db *db, mqtt3_context *context, const char *sub, int sub_qos);
void mqtt3_db_store_clean(mosquitto_db *db);
void mqtt3_db_sys_update(mosquitto_db *db, int interval, time_t start_time);
void mqtt3_db_vacuum(void);

/* ============================================================
 * Subscription functions
 * ============================================================ */
int mqtt3_sub_add(struct _mqtt3_context *context, const char *sub, int qos, struct _mosquitto_subhier *root);
int mqtt3_sub_remove(struct _mqtt3_context *context, const char *sub, struct _mosquitto_subhier *root);
int mqtt3_sub_search(struct _mosquitto_db *db, struct _mosquitto_subhier *root, const char *source_id, const char *topic, int qos, int retain, struct mosquitto_msg_store *stored);
void mqtt3_sub_tree_print(struct _mosquitto_subhier *root, int level);
int mqtt3_subs_clean_session(struct _mqtt3_context *context, struct _mosquitto_subhier *root);

/* ============================================================
 * Context functions
 * ============================================================ */
mqtt3_context *mqtt3_context_init(int sock);
void mqtt3_context_cleanup(mosquitto_db *db, mqtt3_context *context, bool do_free);

/* ============================================================
 * Logging functions
 * ============================================================ */
int mqtt3_log_init(int level, int destinations);
int mqtt3_log_close(void);
int mqtt3_log_printf(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* ============================================================
 * Bridge functions
 * ============================================================ */
int mqtt3_bridge_new(mosquitto_db *db, struct _mqtt3_bridge *bridge);
int mqtt3_bridge_connect(mosquitto_db *db, mqtt3_context *context);
void mqtt3_bridge_packet_cleanup(mqtt3_context *context);

/* ============================================================
 * Security related functions
 * ============================================================ */
int mqtt3_aclfile_parse(struct _mosquitto_db *db);
int mqtt3_acl_check(struct _mosquitto_db *db, mqtt3_context *context, const char *topic, int access);
int mqtt3_pwfile_parse(struct _mosquitto_db *db);
int mqtt3_unpwd_check(struct _mosquitto_db *db, const char *username, const char *password);
int mqtt3_unpwd_cleanup(struct _mosquitto_db *db);

#endif
