/*
Copyright (c) 2010 Roger Light <roger@atchoo.org>
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

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <config.h>
#include <memory_mosq.h>
#include <mqtt3.h>

/* DB read/write */
const unsigned char magic[15] = {0x00, 0xB5, 0x00, 'm','o','s','q','u','i','t','t','o',' ','d','b'};
#define DB_CHUNK_CFG 1
#define DB_CHUNK_MSG_STORE 2
#define DB_CHUNK_CLIENT_MSG 3
#define DB_CHUNK_RETAIN 4
#define DB_CHUNK_SUB 5
/* End DB read/write */

#ifdef WITH_SQLITE_UPGRADE
#include <sqlite3.h>
static int mqtt3_db_sqlite_restore(mosquitto_db *db);
#endif
static int _db_restore_sub(mosquitto_db *db, const char *client_id, const char *sub, int qos);


static int mqtt3_db_client_messages_write(mosquitto_db *db, int db_fd, mqtt3_context *context)
{
	uint32_t length;
	uint64_t i64temp;
	uint16_t i16temp;
	uint8_t i8temp;
	mosquitto_client_msg *cmsg;

	assert(db);
	assert(db_fd >= 0);
	assert(context);

#define write_e(a, b, c) if(write(a, b, c) != c){ return 1; }

	cmsg = context->msgs;
	while(cmsg){
		length = htonl(sizeof(uint64_t) + sizeof(uint16_t) + sizeof(uint8_t) +
				sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t) +
				sizeof(uint8_t));

		i16temp = htons(DB_CHUNK_CLIENT_MSG);
		write_e(db_fd, &i16temp, sizeof(uint16_t));
		write_e(db_fd, &length, sizeof(uint32_t));

		i64temp = htobe64(cmsg->store->db_id);
		write_e(db_fd, &i64temp, sizeof(uint64_t));

		i16temp = htons(cmsg->mid);
		write_e(db_fd, &i16temp, sizeof(uint16_t));

		i8temp = (uint8_t )cmsg->qos;
		write_e(db_fd, &i8temp, sizeof(uint16_t));

		i8temp = (uint8_t )cmsg->retain;
		write_e(db_fd, &i8temp, sizeof(uint16_t));

		i8temp = (uint8_t )cmsg->direction;
		write_e(db_fd, &i8temp, sizeof(uint16_t));

		i8temp = (uint8_t )cmsg->state;
		write_e(db_fd, &i8temp, sizeof(uint16_t));

		i8temp = (uint8_t )cmsg->dup;
		write_e(db_fd, &i8temp, sizeof(uint16_t));

		cmsg = cmsg->next;
	}

#undef write_e

	return 0;
}


static int mqtt3_db_message_store_write(mosquitto_db *db, int db_fd)
{
	uint32_t length;
	uint64_t i64temp;
	uint32_t i32temp;
	uint16_t i16temp;
	uint8_t i8temp;
	struct mosquitto_msg_store *stored;

	assert(db);
	assert(db_fd >= 0);

#define write_e(a, b, c) if(write(a, b, c) != c){ return 1; }

	stored = db->msg_store;
	while(stored){
		length = htonl(sizeof(uint64_t) + 2+strlen(stored->source_id) +
				sizeof(uint16_t) + sizeof(uint16_t) +
				2+strlen(stored->msg.topic) + sizeof(uint32_t) +
				stored->msg.payloadlen + sizeof(uint8_t) + sizeof(uint8_t));

		i16temp = htons(DB_CHUNK_MSG_STORE);
		write_e(db_fd, &i16temp, sizeof(uint16_t));
		write_e(db_fd, &length, sizeof(uint32_t));

		i64temp = htobe64(stored->db_id);
		write_e(db_fd, &i64temp, sizeof(uint64_t));

		i16temp = htons(strlen(stored->source_id));
		write_e(db_fd, &i16temp, sizeof(uint16_t));
		if(i16temp){
			write_e(db_fd, stored->source_id, strlen(stored->source_id));
		}

		i16temp = htons(stored->source_mid);
		write_e(db_fd, &i16temp, sizeof(uint16_t));

		i16temp = htons(stored->msg.mid);
		write_e(db_fd, &i16temp, sizeof(uint16_t));

		i16temp = htons(strlen(stored->msg.topic));
		write_e(db_fd, &i16temp, sizeof(uint16_t));
		write_e(db_fd, stored->msg.topic, strlen(stored->msg.topic));

		i8temp = (uint8_t )stored->msg.qos;
		write_e(db_fd, &i8temp, sizeof(uint8_t));

		i8temp = (uint8_t )stored->msg.retain;
		write_e(db_fd, &i8temp, sizeof(uint8_t));

		i32temp = htonl(stored->msg.payloadlen);
		write_e(db_fd, &i32temp, sizeof(uint32_t));
		if(stored->msg.payloadlen){
			write_e(db_fd, stored->msg.payload, stored->msg.payloadlen);
		}

		stored = stored->next;
	}

#undef write_e

	return 0;
}

static int mqtt3_db_client_write(mosquitto_db *db, int db_fd)
{
	int i;
	uint32_t length;
	uint64_t i64temp;
	uint32_t i32temp;
	uint16_t i16temp;
	uint8_t i8temp;
	mqtt3_context *context;

	assert(db);
	assert(db_fd >= 0);

#define write_e(a, b, c) if(write(a, b, c) != c){ return 1; }

	for(i=0; i<db->context_count; i++){
		context = db->contexts[i];
		if(context){
			if(mqtt3_db_client_messages_write(db, db_fd, context)) return 1;
		}
	}
#undef write_e

	return 0;
}

static int _db_subs_write(mosquitto_db *db, int db_fd, struct _mosquitto_subhier *node, const char *topic)
{
	struct _mosquitto_subhier *subhier;
	struct _mosquitto_subleaf *sub;
	char *thistopic;
	uint32_t length;
	uint16_t i16temp;
	int slen;

	slen = strlen(topic) + strlen(node->topic) + 1;
	thistopic = _mosquitto_malloc(sizeof(char)*slen);
	if(!thistopic) return 1;
	snprintf(thistopic, slen, "%s/%s", topic, node->topic);

#define write_e(a, b, c) if(write(a, b, c) != c){ return 1; }
	sub = node->subs;
	while(sub){
		if(sub->context->clean_session == false){
			length = htonl(2+strlen(sub->context->core.id) + 2+strlen(thistopic) + sizeof(uint8_t));

			i16temp = htons(DB_CHUNK_SUB);
			write_e(db_fd, &i16temp, sizeof(uint16_t));
			write_e(db_fd, &length, sizeof(uint32_t));

			slen = strlen(sub->context->core.id);
			i16temp = htons(slen);
			write_e(db_fd, &i16temp, sizeof(uint16_t));
			write_e(db_fd, sub->context->core.id, slen);

			slen = strlen(thistopic);
			i16temp = htons(slen);
			write_e(db_fd, &i16temp, sizeof(uint16_t));
			write_e(db_fd, thistopic, slen);

			write_e(db_fd, &sub->qos, sizeof(uint8_t));
		}
		sub = sub->next;
	}
#undef write_e

	subhier = node->children;
	while(subhier){
		_db_subs_write(db, db_fd, subhier, thistopic);
		subhier = subhier->next;
	}
	_mosquitto_free(thistopic);
	return 0;
}

static int mqtt3_db_subs_write(mosquitto_db *db, int db_fd)
{
	struct _mosquitto_subhier *subhier;

	subhier = db->subs.children;
	while(subhier){
		_db_subs_write(db, db_fd, subhier, subhier->topic);
		subhier = subhier->next;
	}
	
	return 0;
}

int mqtt3_db_backup(mosquitto_db *db, bool cleanup, bool shutdown)
{
	int rc = 0;
	int db_fd;
	uint32_t db_version = htonl(MQTT_DB_VERSION);
	uint32_t crc = htonl(0);
	uint64_t i64temp;
	uint32_t i32temp;
	uint16_t i16temp;
	uint8_t i8temp;

	if(!db || !db->filepath) return 1;
	mqtt3_log_printf(MOSQ_LOG_INFO, "Saving in-memory database to %s.", db->filepath);
	if(cleanup){
		mqtt3_db_store_clean(db);
	}

	db_fd = open(db->filepath, O_WRONLY|O_CREAT|O_TRUNC, S_IWUSR|S_IRUSR);
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
	/* chunk length */
	i32temp = htons(sizeof(uint16_t) + sizeof(uint8_t)); // FIXME
	write_e(db_fd, &i32temp, sizeof(uint32_t));
	/* db written at broker shutdown or not */
	i8temp = shutdown;
	write_e(db_fd, &i8temp, sizeof(uint8_t));
	/* last db mid */
	i64temp = htobe64(db->last_db_id);
	write_e(db_fd, &i64temp, sizeof(uint64_t));

	if(mqtt3_db_message_store_write(db, db_fd)){
		goto error;
	}
#undef write_e

	mqtt3_db_subs_write(db, db_fd);

	/* FIXME - needs implementing */
	close(db_fd);
	return rc;
error:
	mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	if(db_fd >= 0) close(db_fd);
	return 1;
}

int mqtt3_db_restore(mosquitto_db *db)
{
	int fd;
	char header[15];
	int rc = 0;

	assert(db);
	assert(db->filepath);

	fd = open(db->filepath, O_RDONLY);
	if(fd < 0) return 1;
	if(read(fd, &header, 15) != 15){
		close(fd);
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Database file header incorrect, not loading.");
		return 1;
	}
	if(!memcmp(header, magic, 15)){
		// Restore DB as normal
	}else if(!memcmp(header, "SQLite format 3", 15)){
		// Restore old sqlite format DB
#ifdef WITH_SQLITE_UPGRADE
		close(fd);
		return mqtt3_db_sqlite_restore(db);
#else
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Upgrading from sqlite databases not supported. Remove database file manually or compile with sqlite support.");
		rc = 1;
#endif
	}else{
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Unable to restore persistent database. Unrecognised file format.");
		rc = 1;
	}

	return rc;
}

static int _db_restore_sub(mosquitto_db *db, const char *client_id, const char *sub, int qos)
{
	mqtt3_context *context;
	mqtt3_context **tmp_contexts;
	int i;

	assert(db);
	assert(client_id);
	assert(sub);

	context = NULL;
	for(i=0; i<db->context_count; i++){
		if(db->contexts[i] && !strcmp(db->contexts[i]->core.id, client_id)){
			context = db->contexts[i];
			break;
		}
	}
	if(!context){
		context = mqtt3_context_init(-1);

		for(i=0; i<db->context_count; i++){
			if(!db->contexts[i]){
				db->contexts[i] = context;
				break;
			}
		}
		if(i==db->context_count){
			db->context_count++;
			tmp_contexts = _mosquitto_realloc(db->contexts, sizeof(mqtt3_context*)*db->context_count);
			if(tmp_contexts){
				db->contexts = tmp_contexts;
				db->contexts[db->context_count-1] = context;
			}else{
				return 1;
			}
		}
		context->core.id = _mosquitto_strdup(client_id);
	}
	return mqtt3_sub_add(context, sub, qos, &db->subs);
}

#ifdef WITH_SQLITE_UPGRADE
static int mqtt3_db_sqlite_restore(mosquitto_db *db)
{
	sqlite3 *sql_db;
	sqlite3_stmt *stmt = NULL;
	const char *topic, *source_id, *sub, *client_id;
	int qos;
	int payloadlen;
	const uint8_t *payload;
	struct mosquitto_msg_store *stored;
	int version;

	assert(db);

	if(sqlite3_open_v2(db->filepath, &sql_db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK){
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Unable to open sqlite database located at %s for upgrading.", db->filepath);
		return 1;
	}

	if(sqlite3_prepare_v2(sql_db, "SELECT value FROM config WHERE key='version'", -1, &stmt, NULL) == SQLITE_OK){
		if(sqlite3_step(stmt) == SQLITE_ROW){
			version = sqlite3_column_int(stmt, 0);
			if(version != 2){
				mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Unable to upgrade from sqlite database version %d.", version);
				sqlite3_finalize(stmt);
				sqlite3_close(sql_db);
				return 1;
			}
		}else{
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Unable to upgrade from this sqlite database, it appears to be corrupted.");
			sqlite3_finalize(stmt);
			sqlite3_close(sql_db);
			return 1;
		}
		sqlite3_finalize(stmt);
	}else{
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Problem communicating with sqlite.");
		return 1;
	}

	if(sqlite3_prepare_v2(sql_db, "SELECT retain.topic, message_store.qos,"
			"message_store.payloadlen, message_store.payload, message_store.source_id "
			"FROM retain JOIN message_store on retain.topic=message_store.topic", -1, &stmt, NULL) == SQLITE_OK){

		while(sqlite3_step(stmt) == SQLITE_ROW){
			topic = (const char *)sqlite3_column_text(stmt, 0);
			qos = sqlite3_column_int(stmt, 1);
			payloadlen = sqlite3_column_int(stmt, 2);
			payload = sqlite3_column_blob(stmt, 3);
			source_id = (const char *)sqlite3_column_text(stmt, 4);

			mqtt3_db_message_store(db, source_id, 0, topic, qos, payloadlen, payload, 1, &stored);
			mqtt3_sub_search(&db->subs, source_id, topic, qos, 1, stored);
		}
		sqlite3_finalize(stmt);
	}else{
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Problem communicating with sqlite.");
		return 1;
	}

	if(sqlite3_prepare_v2(sql_db, "SELECT client_id, sub, qos FROM subs "
			"JOIN clients ON subs.client_id=clients.id WHERE clients.clean_session=0",
			-1, &stmt, NULL) == SQLITE_OK){

		while(sqlite3_step(stmt) == SQLITE_ROW){
			client_id = (const char *)sqlite3_column_text(stmt, 0);
			sub = (const char *)sqlite3_column_text(stmt, 1);
			qos = sqlite3_column_int(stmt, 2);

			_db_restore_sub(db, client_id, sub, qos);
		}
		sqlite3_finalize(stmt);
	}else{
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Problem communicating with sqlite.");
		return 1;
	}
	return 0;
}
#endif
