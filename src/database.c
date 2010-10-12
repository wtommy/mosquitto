/*
Copyright (c) 2009,2010, Roger Light <roger@atchoo.org>
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

/* A note on matching topic subscriptions.
 *
 * Topics can be up to 32767 characters in length. The / character is used as a
 * hierarchy delimiter. Messages are published to a particular topic.
 * Clients may subscribe to particular topics directly, but may also use
 * wildcards in subscriptions.  The + and # characters are used as wildcards.
 * The # wildcard can be used at the end of a subscription only, and is a
 * wildcard for the level of hierarchy at which it is placed and all subsequent
 * levels.
 * The + wildcard may be used at any point within the subscription and is a
 * wildcard for only the level of hierarchy at which it is placed.
 * Neither wildcard may be used as part of a substring.
 * Valid:
 * 	a/b/+
 * 	a/+/c
 * 	a/#
 * 	a/b/#
 * 	#
 * 	+/b/c
 * 	+/+/+
 * Invalid:
 *	a/#/c
 *	a+/b/c
 * Valid but non-matching:
 *	a/b
 *	a/+
 *	+/b
 *	b/c/a
 *	a/b/d
 */

#include <arpa/inet.h>
#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <config.h>
#include <mqtt3.h>
#include <memory_mosq.h>
#include <subs.h>
#include <util_mosq.h>

/* DB read/write */
const unsigned char magic[15] = {0x00, 0xB5, 0x00, 'm','o','s','q','u','i','t','t','o',' ','d','b'};
#define DB_CHUNK_CFG 1
/* End DB read/write */

static char *db_filepath = NULL;

static int max_inflight = 20;
static int max_queued = 100;

static int _mqtt3_db_cleanup(mosquitto_db *db);

int mqtt3_db_open(mqtt3_config *config, mosquitto_db *db)
{
	int rc = 0;
	FILE *fptr;
	struct _mosquitto_subhier *child;

	if(!config) return 1;

	if(config->persistence){
		if(config->persistence_location && strlen(config->persistence_location)){
			db_filepath = _mosquitto_malloc(strlen(config->persistence_location) + strlen(config->persistence_file) + 1);
			if(!db_filepath) return 1;
			sprintf(db_filepath, "%s%s", config->persistence_location, config->persistence_file);
		}else{
			db_filepath = _mosquitto_strdup(config->persistence_file);
		}
	}

	db->last_mid = 0;

	db->context_count = 1;
	db->contexts = _mosquitto_malloc(sizeof(mqtt3_context*)*db->context_count);
	if(!db->contexts) return MOSQ_ERR_NOMEM;
	db->contexts[0] = NULL;

	db->subs.next = NULL;
	db->subs.subs = NULL;
	db->subs.topic = "";

	child = _mosquitto_malloc(sizeof(struct _mosquitto_subhier));
	child->next = NULL;
	child->topic = _mosquitto_strdup("");
	child->subs = NULL;
	child->children = NULL;
	child->retained = NULL;
	db->subs.children = child;

	child = _mosquitto_malloc(sizeof(struct _mosquitto_subhier));
	child->next = NULL;
	child->topic = _mosquitto_strdup("/");
	child->subs = NULL;
	child->children = NULL;
	child->retained = NULL;
	db->subs.children->next = child;

	child = _mosquitto_malloc(sizeof(struct _mosquitto_subhier));
	child->next = NULL;
	child->topic = _mosquitto_strdup("$SYS");
	child->subs = NULL;
	child->children = NULL;
	child->retained = NULL;
	db->subs.children->next->next = child;

	if(_mqtt3_db_cleanup(db)) return 1;

	return rc;
}

static void subhier_clean(struct _mosquitto_subhier *subhier)
{
	struct _mosquitto_subhier *next;
	struct _mosquitto_subleaf *leaf, *nextleaf;

	while(subhier){
		next = subhier->next;
		leaf = subhier->subs;
		while(leaf){
			nextleaf = leaf->next;
			_mosquitto_free(leaf);
			leaf = nextleaf;
		}
		if(subhier->retained){
			subhier->retained->ref_count--;
		}
		subhier_clean(subhier->children);
		if(subhier->topic) _mosquitto_free(subhier->topic);

		_mosquitto_free(subhier);
		subhier = next;
	}
}

int mqtt3_db_close(mosquitto_db *db)
{
	subhier_clean(db->subs.children);
	mqtt3_db_store_clean(db);

	if(db_filepath) _mosquitto_free(db_filepath);

	return MOSQ_ERR_SUCCESS;
}

int mqtt3_db_backup(mosquitto_db *db, bool cleanup)
{
	int rc = 0;
	int db_fd;
	uint32_t db_version = htonl(MQTT_DB_VERSION);
	uint32_t crc = htonl(0);
	uint64_t i64temp;
	uint32_t i32temp;
	uint16_t i16temp;

	if(!db || !db_filepath) return 1;
	mqtt3_log_printf(MOSQ_LOG_INFO, "Saving in-memory database to %s.", db_filepath);
	if(cleanup){
		mqtt3_db_store_clean(db);
	}

	db_fd = open(db_filepath, O_WRONLY|O_CREAT|O_TRUNC, S_IWUSR|S_IRUSR);
	if(db_fd < 0){
		goto error;
	}

#define write_e(a, b, c) if(write(a, b, c) != c){ goto error; }

	/* Header */
	write_e(db_fd, magic, 15);
	write_e(db_fd, &crc, sizeof(uint32_t));
	write_e(db_fd, &db_version, sizeof(uint32_t));

	/* FIXME - what more config is needed? */
	/* DB config */
	i16temp = htons(DB_CHUNK_CFG);
	write_e(db_fd, &i16temp, sizeof(uint16_t));
	/* chunk size */
	i16temp = htons(sizeof(uint16_t)); // FIXME
	write_e(db_fd, &i16temp, sizeof(uint16_t));
	/* last db mid */
	i64temp = htobe64(db->last_mid);
	write_e(db_fd, &i64temp, sizeof(uint64_t));

#undef write_e

	/* FIXME - needs implementing */
	close(db_fd);
	return rc;
error:
	mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	if(db_fd >= 0) close(db_fd);
	return 1;
}

/* Called on client death to add a will to the message queue if the will exists.
 * Returns 1 on failure (context or context->core.id is NULL)
 * Returns 0 on success (will queued or will not found)
 */
int mqtt3_db_client_will_queue(mosquitto_db *db, mqtt3_context *context)
{
	if(!context || !context->core.id) return 1;
	if(!context->core.will) return 0;

	return mqtt3_db_messages_easy_queue(db, context, context->core.will->topic, context->core.will->qos, context->core.will->payloadlen, context->core.will->payload, context->core.will->retain);
}

/* Returns the number of client currently in the database.
 * This includes inactive clients.
 * Returns 1 on failure (count is NULL)
 * Returns 0 on success.
 */
int mqtt3_db_client_count(mosquitto_db *db, int *count)
{
	int i;

	if(!db || !count) return 1;

	*count = 0;
	for(i=0; i<db->context_count; i++){
		if(db->contexts[i]) (*count)++;
	}

	return 0;
}

/* Internal function.
 * Set all stored sockets to -1 (invalid) when starting mosquitto.
 * Also removes any stray clients and subcriptions that may be around from a prior crash.
 * Returns 1 on failure (db is NULL)
 * Returns 0 on success.
 */
static int _mqtt3_db_cleanup(mosquitto_db *db)
{
	int rc = 0;

	if(!db) return 1;

// FIXME - reimplement for new db
#if 0
	query = sqlite3_mprintf("UPDATE clients SET sock=-1");
	/* Remove any stray clients that have clean session set. */
	query = sqlite3_mprintf("DELETE FROM clients WHERE sock=-1 AND clean_session=1");
	/* Remove any subs with no client. */
	query = sqlite3_mprintf("DELETE FROM subs WHERE client_id NOT IN (SELECT id FROM clients)");
	/* Remove any messages with no client. */
	query = sqlite3_mprintf("DELETE FROM messages WHERE client_id NOT IN (SELECT id FROM clients)");
#endif
	return rc;
}

int mqtt3_db_message_delete(mqtt3_context *context, uint16_t mid, enum mosquitto_msg_direction dir)
{
	mosquitto_client_msg *tail, *last = NULL;

	if(!context) return 1;

	tail = context->msgs;
	while(tail){
		if(tail->mid == mid && tail->direction == dir){
			/* FIXME - it would be nice to be able to remove the stored message here if rec_count==0 */
			tail->store->ref_count--;
			if(last){
				last->next = tail->next;
			}else{
				context->msgs = tail->next;
			}
			_mosquitto_free(tail);
			return 0;
		}
		last = tail;
		tail = tail->next;
	}

	return 0;
}

int mqtt3_db_message_insert(mqtt3_context *context, uint16_t mid, enum mosquitto_msg_direction dir, enum mqtt3_msg_state state, int qos, bool retain, struct mosquitto_msg_store *stored)
{
	mosquitto_client_msg *msg, *tail;
	int count = 0;
	int sock = -1;
	int limited = 0;

	assert(stored);
	if(!context) return 1;

#if 0
// FIXME - reimplement for new database
	if(!count_stmt){
		count_stmt = _mqtt3_db_statement_prepare("SELECT "
				"(SELECT COUNT(*) FROM messages WHERE client_id=?),"
				"(SELECT sock FROM clients WHERE id=?)");
		if(!count_stmt){
			return 1;
		}
	}
	if(max_inflight || max_queued){
		if(sqlite3_bind_text(count_stmt, 1, context->core.id, strlen(context->core.id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_text(count_stmt, 2, context->core.id, strlen(context->core.id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_step(count_stmt) == SQLITE_ROW){
			count = sqlite3_column_int(count_stmt, 0);
			sock = sqlite3_column_int(count_stmt, 1);

			if(sock == -1){
				if(max_queued > 0 && count >= max_queued) limited = 1;
			}else{
				if(max_inflight > 0 && count >= max_inflight) limited = 1;
			}
		}
		sqlite3_reset(count_stmt);
		sqlite3_clear_bindings(count_stmt);
		if(limited) return 2;
	}
#endif
	msg = _mosquitto_malloc(sizeof(mosquitto_client_msg));
	if(!msg) return 1;
	msg->next = NULL;
	msg->store = stored;
	msg->store->ref_count++;
	msg->mid = mid;
	msg->timestamp = time(NULL);
	msg->direction = dir;
	msg->state = state;
	msg->dup = false;
	msg->qos = qos;
	msg->retain = retain;
	tail = context->msgs;
	while(tail && tail->next){
		tail = tail->next;
	}
	if(tail){
		tail->next = msg;
	}else{
		context->msgs = msg;
	}

	return 0;
}

int mqtt3_db_message_update(mqtt3_context *context, uint16_t mid, enum mosquitto_msg_direction dir, enum mqtt3_msg_state state)
{
	mosquitto_client_msg *tail;

	tail = context->msgs;
	while(tail){
		if(tail->mid == mid && tail->direction == dir){
			tail->state = state;
			tail->timestamp = time(NULL);
			return 0;
		}
		tail = tail->next;
	}
	return 1;
}

int mqtt3_db_messages_delete(mqtt3_context *context)
{
	mosquitto_client_msg *tail, *next;

	if(!context) return 1;

	tail = context->msgs;
	while(tail){
		/* FIXME - it would be nice to be able to remove the stored message here if rec_count==0 */
		tail->store->ref_count--;
		next = tail->next;
		_mosquitto_free(tail);
		tail = next;
	}
	context->msgs = NULL;

	return 0;
}

int mqtt3_db_messages_easy_queue(mosquitto_db *db, mqtt3_context *context, const char *topic, int qos, uint32_t payloadlen, const uint8_t *payload, int retain)
{
	struct mosquitto_msg_store *stored;
	char *source_id;

	assert(db);

	if(!topic) return 1;

	if(context){
		source_id = context->core.id;
	}else{
		source_id = "";
	}
	if(mqtt3_db_message_store(db, source_id, 0, topic, qos, payloadlen, payload, retain, &stored)) return 1;

	return mqtt3_db_messages_queue(db, source_id, topic, qos, retain, stored);
}

int mqtt3_db_messages_queue(mosquitto_db *db, const char *source_id, const char *topic, int qos, int retain, struct mosquitto_msg_store *stored)
{
	int rc = 0;

	assert(db);
	assert(stored);

	/* Find all clients that subscribe to topic and put messages into the db for them. */
	if(!source_id || !topic) return 1;

	mqtt3_sub_search(&db->subs, source_id, topic, qos, retain, stored);
	return rc;
}

int mqtt3_db_message_store(mosquitto_db *db, const char *source, uint16_t source_mid, const char *topic, int qos, uint32_t payloadlen, const uint8_t *payload, int retain, struct mosquitto_msg_store **stored)
{
	struct mosquitto_msg_store *temp;

	assert(db);
	assert(stored);

	if(!topic) return 1;

	temp = _mosquitto_malloc(sizeof(struct mosquitto_msg_store));
	if(!temp) return 1;

	temp->next = db->msg_store;
	temp->timestamp = time(NULL);
	temp->ref_count = 0;
	temp->source_id = _mosquitto_strdup(source);
	temp->source_mid = source_mid;
	temp->msg.qos = qos;
	temp->msg.retain = retain;
	temp->msg.topic = _mosquitto_strdup(topic);
	temp->msg.payloadlen = payloadlen;
	if(payloadlen){
		temp->msg.payload = _mosquitto_malloc(sizeof(uint8_t)*payloadlen);
		memcpy(temp->msg.payload, payload, sizeof(uint8_t)*payloadlen);
	}else{
		temp->msg.payload = NULL;
	}

	if(!temp->source_id || !temp->msg.topic || (payloadlen && !temp->msg.payload)){
		if(temp->source_id) _mosquitto_free(temp->source_id);
		if(temp->msg.topic) _mosquitto_free(temp->msg.topic);
		if(temp->msg.payload) _mosquitto_free(temp->msg.payload);
		_mosquitto_free(temp);
		return 1;
	}
	db->msg_store_count++;
	db->msg_store = temp;
	(*stored) = temp;

	return 0;
}

int mqtt3_db_message_store_find(mosquitto_db *db, const char *source, uint16_t mid, struct mosquitto_msg_store **stored)
{
	struct mosquitto_msg_store *tail;

	*stored = NULL;
	tail = db->msg_store;
	while(tail){
		if(tail->source_mid == mid && !strcmp(tail->source_id, source)){
			*stored = tail;
			return 0;
		}
		tail = tail->next;
	}

	return 1;
}

int mqtt3_db_message_timeout_check(mosquitto_db *db, unsigned int timeout)
{
	int i;
	time_t threshold = time(NULL) - timeout;
	enum mqtt3_msg_state new_state = ms_invalid;
	mqtt3_context *context;
	mosquitto_client_msg *msg;

	
	for(i=0; i<db->context_count; i++){
		context = db->contexts[i];
		if(!context) continue;

		msg = context->msgs;
		while(msg){
			if(msg->timestamp < threshold){
				switch(msg->state){
					case ms_wait_puback:
						new_state = ms_publish_puback;
						break;
					case ms_wait_pubrec:
						new_state = ms_publish_pubrec;
						break;
					case ms_wait_pubrel:
						new_state = ms_resend_pubrec;
						break;
					case ms_wait_pubcomp:
						new_state = ms_resend_pubrel;
						break;
					default:
						break;
				}
				if(new_state != ms_invalid){
					msg->timestamp = time(NULL);
					msg->state = new_state;
					msg->dup = true;
				}
			}
			msg = msg->next;
		}
	}

	return MOSQ_ERR_SUCCESS;
}

int mqtt3_db_message_release(mosquitto_db *db, mqtt3_context *context, uint16_t mid, enum mosquitto_msg_direction dir)
{
	mosquitto_client_msg *tail, *last = NULL;
	int qos;
	int retain;
	char *topic;
	char *source_id;

	if(!context) return 1;

	tail = context->msgs;
	while(tail){
		if(tail->mid == mid && tail->direction == dir){
			qos = tail->store->msg.qos;
			topic = tail->store->msg.topic;
			retain = tail->retain;
			source_id = tail->store->source_id;

			if(!mqtt3_db_messages_queue(db, source_id, topic, qos, retain, tail->store)){
				tail->store->ref_count--;
				if(last){
					last->next = tail->next;
				}else{
					context->msgs = tail->next;
				}
				_mosquitto_free(tail);
				return 0;
			}else{
				return 1;
			}
		}
		last = tail;
		tail = tail->next;
	}
	return 1;
}

int mqtt3_db_message_write(mqtt3_context *context)
{
	mosquitto_client_msg *tail, *last = NULL;
	uint16_t mid;
	int retries;
	int retain;
	const char *topic;
	int qos;
	uint32_t payloadlen;
	const uint8_t *payload;

	if(!context || !context->core.id || context->core.sock == -1) return 1;

	tail = context->msgs;
	while(tail){
		if(tail->direction == mosq_md_out){
			mid = tail->mid;
			retries = tail->dup;
			retain = tail->retain;
			topic = tail->store->msg.topic;
			qos = tail->qos;
			payloadlen = tail->store->msg.payloadlen;
			payload = tail->store->msg.payload;

			switch(tail->state){
				case ms_publish:
					if(!mqtt3_raw_publish(context, retries, qos, retain, mid, topic, payloadlen, payload)){
						if(last){
							last->next = tail->next;
							tail->store->ref_count--;
							tail = last->next;
							_mosquitto_free(tail);
						}else{
							context->msgs = tail->next;
							tail->store->ref_count--;
							_mosquitto_free(tail);
							tail = context->msgs;
						}
					}
					break;

				case ms_publish_puback:
					if(!mqtt3_raw_publish(context, retries, qos, retain, mid, topic, payloadlen, payload)){
						tail->state = ms_wait_puback;
					}
					last = tail;
					tail = tail->next;
					break;

				case ms_publish_pubrec:
					if(!mqtt3_raw_publish(context, retries, qos, retain, mid, topic, payloadlen, payload)){
						tail->state = ms_wait_pubrec;
					}
					last = tail;
					tail = tail->next;
					break;
				
				case ms_resend_pubrec:
					if(!mqtt3_raw_pubrec(context, mid)){
						tail->state = ms_wait_pubrel;
					}
					last = tail;
					tail = tail->next;
					break;

				case ms_resend_pubrel:
					if(!mqtt3_raw_pubrel(context, mid)){
						tail->state = ms_wait_pubcomp;
					}
					last = tail;
					tail = tail->next;
					break;

				case ms_resend_pubcomp:
					if(!mqtt3_raw_pubcomp(context, mid)){
						tail->state = ms_wait_pubrel;
					}
					last = tail;
					tail = tail->next;
					break;

				default:
					last = tail;
					tail = tail->next;
					break;
			}
		}else{
			last = tail;
			tail = tail->next;
		}
	}

	return 0;
}

void mqtt3_db_store_clean(mosquitto_db *db)
{
	/* FIXME - this may not be necessary if checks are made when messages are removed. */
	struct mosquitto_msg_store *tail, *last = NULL;
	assert(db);

	tail = db->msg_store;
	while(tail){
		if(tail->ref_count == 0){
			if(tail->source_id) _mosquitto_free(tail->source_id);
			if(tail->msg.topic) _mosquitto_free(tail->msg.topic);
			if(tail->msg.payload) _mosquitto_free(tail->msg.payload);
			if(last){
				last->next = tail->next;
				_mosquitto_free(tail);
				tail = last->next;
			}else{
				db->msg_store = tail->next;
				_mosquitto_free(tail);
				tail = db->msg_store;
			}
		}else{
			last = tail;
			tail = tail->next;
		}
	}
}

/* Send messages for the $SYS hierarchy if the last update is longer than
 * 'interval' seconds ago.
 * 'interval' is the amount of seconds between updates. If 0, then no periodic
 * messages are sent for the $SYS hierarchy.
 * 'start_time' is the result of time() that the broker was started at.
 */
void mqtt3_db_sys_update(mosquitto_db *db, int interval, time_t start_time)
{
	static time_t last_update = 0;
	time_t now = time(NULL);
	char buf[100];
	int count;

	if(interval && now - interval > last_update){
		snprintf(buf, 100, "%d seconds", (int)(now - start_time));
		mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/uptime", 2, strlen(buf), (uint8_t *)buf, 1);

		snprintf(buf, 100, "%d", db->msg_store_count);
		mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/messages/inflight", 2, strlen(buf), (uint8_t *)buf, 1);

		if(!mqtt3_db_client_count(db, &count)){
			snprintf(buf, 100, "%d", count);
			mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/clients/total", 2, strlen(buf), (uint8_t *)buf, 1);
		}

#ifdef WITH_MEMORY_TRACKING
		snprintf(buf, 100, "%ld", _mosquitto_memory_used());
		mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/heap/current size", 2, strlen(buf), (uint8_t *)buf, 1);
#endif

		snprintf(buf, 100, "%lu", mqtt3_net_msgs_total_received());
		mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/messages/received", 2, strlen(buf), (uint8_t *)buf, 1);
		
		snprintf(buf, 100, "%lu", mqtt3_net_msgs_total_sent());
		mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/messages/sent", 2, strlen(buf), (uint8_t *)buf, 1);

		snprintf(buf, 100, "%llu", (unsigned long long)mqtt3_net_bytes_total_received());
		mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/bytes/received", 2, strlen(buf), (uint8_t *)buf, 1);
		
		snprintf(buf, 100, "%llu", (unsigned long long)mqtt3_net_bytes_total_sent());
		mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/bytes/sent", 2, strlen(buf), (uint8_t *)buf, 1);
		
		last_update = time(NULL);
	}
}

void mqtt3_db_limits_set(int inflight, int queued)
{
	max_inflight = inflight;
	max_queued = queued;
}

void mqtt3_db_vacuum(void)
{
	/* FIXME - reimplement? */
}

