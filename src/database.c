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

#include <assert.h>
#include <errno.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

#include <config.h>
#include <mqtt3.h>
#include <memory_mosq.h>
#include <subs.h>
#include <util_mosq.h>

static sqlite3 *db = NULL;
static char *db_filepath = NULL;

static int max_inflight = 20;
static int max_queued = 100;

static int _mqtt3_db_tables_create(void);
static int _mqtt3_db_cleanup(void);
static sqlite3_stmt *_mqtt3_db_statement_prepare(const char *query);
static void _mqtt3_db_statements_finalize(sqlite3 *fdb);
static int _mqtt3_db_version_check(void);
#if defined(WITH_BROKER) && defined(WITH_DB_UPGRADE)
static int _mqtt3_db_upgrade(void);
static int _mqtt3_db_upgrade_1_2(void);
#endif

int mqtt3_db_open(mqtt3_config *config)
{
	int dbrc;
	int rc = 0;
	sqlite3_backup *restore;
	sqlite3 *restore_db;
	FILE *fptr;

	if(!config) return 1;
	if(sqlite3_initialize() != SQLITE_OK){
		return 1;
	}

	if(sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK){
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s", sqlite3_errmsg(db));
		return 1;
	}

	if(!config->persistence){
		if(_mqtt3_db_tables_create()) return 1;
	}else{
		if(config->persistence_location && strlen(config->persistence_location)){
			db_filepath = _mosquitto_malloc(strlen(config->persistence_location) + strlen(config->persistence_file) + 1);
			if(!db_filepath) return 1;
			sprintf(db_filepath, "%s%s", config->persistence_location, config->persistence_file);
		}else{
			db_filepath = _mosquitto_strdup(config->persistence_file);
		}
		dbrc = sqlite3_open_v2(db_filepath, &restore_db, SQLITE_OPEN_READONLY, NULL);
		if(dbrc == SQLITE_OK){
			restore = sqlite3_backup_init(db, "main", restore_db, "main");
			if(restore){
				sqlite3_backup_step(restore, -1);
				sqlite3_backup_finish(restore);
				if(_mqtt3_db_version_check()){
#if defined(WITH_BROKER) && defined(WITH_DB_UPGRADE)
					if(_mqtt3_db_upgrade()){
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Unable to upgrade database.");
						return 1;
					}
#else
					mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid database version.");
					return 1;
#endif
				}
			}else{
				mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Couldn't restore database %s (%d).", db_filepath, dbrc);
				return 1;
			}
			sqlite3_close(restore_db);
		}else{
			fptr = fopen(db_filepath, "rb");
			if(!fptr){
				switch(errno){
					case ENOENT:
						/* File doesn't exist - ok to create */
						if(_mqtt3_db_tables_create()){
							mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Unable to populate new database. Try restarting mosquitto.");
							return 1;
						}
						break;
					case EACCES:
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Permission denied trying to restore persistent database %s.", db_filepath);
						return 1;
					default:
						mqtt3_log_printf(MOSQ_LOG_ERR, "%s", strerror(errno));
						return 1;
				}
			}else{
				mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Possibly corrupt database file. Try restarting mosquitto.");
				fclose(fptr);
			}
		}
	}

	if(_mqtt3_db_cleanup()) return 1;

	return rc;
}

int mqtt3_db_close(void)
{
	_mqtt3_db_statements_finalize(db);

	sqlite3_close(db);
	db = NULL;

	sqlite3_shutdown();

	if(db_filepath) _mosquitto_free(db_filepath);

	return MOSQ_ERR_SUCCESS;
}

int mqtt3_db_backup(mosquitto_db *db, bool cleanup)
{
	int rc = 0;

	if(!db || !db_filepath) return 1;
	mqtt3_log_printf(MOSQ_LOG_INFO, "Saving in-memory database to %s.", db_filepath);
	if(cleanup){
		mqtt3_db_store_clean(db);
	}
	/* FIXME - needs implementing */

	return rc;
}

static int _mqtt3_db_tables_create(void)
{
	int rc = 0;
	char *errmsg = NULL;
	char *query;

	if(sqlite3_exec(db,
		"CREATE TABLE IF NOT EXISTS config(key TEXT PRIMARY KEY, value TEXT)",
		NULL, NULL, &errmsg) != SQLITE_OK){

		rc = 1;
	}
	if(errmsg){
		sqlite3_free(errmsg);
		errmsg = NULL;
	}

	query = sqlite3_mprintf("INSERT INTO config (key, value) VALUES('version','%d')", MQTT_DB_VERSION);
	if(query){
		if(sqlite3_exec(db, query, NULL, NULL, &errmsg) != SQLITE_OK){
			rc = 1;
		}
		sqlite3_free(query);
		if(errmsg){
			sqlite3_free(errmsg);
			errmsg = NULL;
		}
	}else{
		return 1;
	}

	if(sqlite3_exec(db,
		"CREATE TABLE IF NOT EXISTS retain("
		"topic TEXT UNIQUE, store_id INTEGER)",
		NULL, NULL, &errmsg) != SQLITE_OK){

		rc = 1;
	}
	if(errmsg){
		sqlite3_free(errmsg);
		errmsg = NULL;
	}

	if(sqlite3_exec(db,
		"CREATE TABLE IF NOT EXISTS message_store("
		"id INTEGER PRIMARY KEY, timestamp INTEGER, qos INTEGER, "
		"retain INTEGER, topic TEXT, payloadlen INTEGER, payload BLOB, source_id TEXT)",
		NULL, NULL, &errmsg) != SQLITE_OK){

		rc = 1;
	}
	if(errmsg){
		sqlite3_free(errmsg);
		errmsg = NULL;
	}

	if(sqlite3_exec(db,
		"CREATE TABLE IF NOT EXISTS messages("
		"client_id TEXT, timestamp INTEGER, direction INTEGER, status INTEGER, "
		"mid INTEGER, retries INTEGER, qos INTEGER, store_id INTEGER)",
		NULL, NULL, &errmsg) != SQLITE_OK){

		rc = 1;
	}
	if(errmsg){
		sqlite3_free(errmsg);
		errmsg = NULL;
	}

	return rc;
}

/* Internal function.
 * Check the version of the open database.
 * Returns 1 on non-match or failure (sqlite error)
 * Returns 0 on version match.
 */
static int _mqtt3_db_version_check(void)
{
	int rc = 0;
	int version;
	sqlite3_stmt *stmt = NULL;

	if(!db) return 1;

	if(sqlite3_prepare_v2(db, "SELECT value FROM config WHERE key='version'",
			-1, &stmt, NULL) == SQLITE_OK){

		if(sqlite3_step(stmt) == SQLITE_ROW){
			version = sqlite3_column_int(stmt, 0);
			if(version != MQTT_DB_VERSION) rc = 1;
		}else{
			rc = 1;
		}
		sqlite3_finalize(stmt);
	}else{
		rc = 1;
	}

	return rc;
}

#if defined(WITH_BROKER) && defined(WITH_DB_UPGRADE)
static int _mqtt3_db_upgrade(void)
{
	int rc = 0;
	sqlite3_stmt *stmt = NULL;
	int version = 0;

	if(!db) return 1;

	do{
		if(sqlite3_prepare_v2(db, "SELECT value FROM config WHERE key='version'",
				-1, &stmt, NULL) == SQLITE_OK){

			if(sqlite3_step(stmt) == SQLITE_ROW){
				version = sqlite3_column_int(stmt, 0);
			}
			sqlite3_finalize(stmt);
		}else{
			rc = 1;
			break;
		}
		switch(version){
			case 0:
				mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Upgrading from DB version 0 not supported.");
				return 1;
			case 1:
				if(_mqtt3_db_upgrade_1_2()){
					rc = 1;
					break;
				}
		}
	}while(!rc && version != MQTT_DB_VERSION);

	return rc;
}

static int _mqtt3_db_upgrade_1_2(void)
{
	sqlite3 *old_db;
	sqlite3_stmt *old_stmt, *new_stmt;
	const char *client_id;
	int rc = 0;
	uint32_t payloadlen;
	const uint8_t *payload;
	const char *topic, *will_message;
	int timestamp, mid, dup, qos, retain, status, direction;
	int64_t store_id;


	old_db = db;
	db = NULL;
	
	mqtt3_log_printf(MOSQ_LOG_NOTICE, "Upgrading database from version 1 to 2.");

	if(sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK){
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s", sqlite3_errmsg(db));
		db = old_db;
		return 1;
	}
	if(_mqtt3_db_tables_create()) return 1;

	/* ---------- New clients table and copy data ---------- */
	if(sqlite3_prepare_v2(db, "INSERT INTO clients (sock,id,clean_session,will,will_retain,will_qos,will_topic,will_message,last_mid,is_bridge) "
			"VALUES (?,?,?,?,?,?,?,?,?,0)", -1, &new_stmt, NULL) != SQLITE_OK){
		sqlite3_close(db);
		db = old_db;
		return 1;
	}
	if(sqlite3_prepare_v2(old_db, "SELECT sock,id,clean_start,will,will_retain,will_qos,will_topic,will_message,last_mid FROM clients",
			-1, &old_stmt, NULL) != SQLITE_OK){
		sqlite3_finalize(new_stmt);
		sqlite3_close(db);
		db = old_db;
		return 1;
	}
	while(sqlite3_step(old_stmt) == SQLITE_ROW){
		if(sqlite3_bind_int(new_stmt, 1, sqlite3_column_int(old_stmt, 0)) != SQLITE_OK) rc = 1;
		client_id = (const char *)sqlite3_column_text(old_stmt, 1);
		if(sqlite3_bind_text(new_stmt, 2, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(new_stmt, 3, sqlite3_column_int(old_stmt, 2)) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(new_stmt, 4, sqlite3_column_int(old_stmt, 3)) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(new_stmt, 5, sqlite3_column_int(old_stmt, 4)) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(new_stmt, 6, sqlite3_column_int(old_stmt, 5)) != SQLITE_OK) rc = 1;
		topic = (const char *)sqlite3_column_text(old_stmt, 6);
		if(sqlite3_bind_text(new_stmt, 7, topic, strlen(topic), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		will_message = (const char *)sqlite3_column_text(old_stmt, 7);
		if(sqlite3_bind_text(new_stmt, 8, will_message, strlen(will_message), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(new_stmt, 9, sqlite3_column_int(old_stmt, 8)) != SQLITE_OK) rc = 1;
		if(sqlite3_step(new_stmt) != SQLITE_DONE) rc = 1;
		sqlite3_reset(new_stmt);
		sqlite3_clear_bindings(new_stmt);
	}
	sqlite3_finalize(new_stmt);
	sqlite3_finalize(old_stmt);

	/* ---------- Copy subs data ---------- */
	if(sqlite3_prepare_v2(db, "INSERT INTO subs (client_id,sub,qos) VALUES (?,?,?)",
			-1, &new_stmt, NULL) != SQLITE_OK){
		sqlite3_close(db);
		db = old_db;
		return 1;
	}
	if(sqlite3_prepare_v2(old_db, "SELECT client_id,sub,qos FROM subs",
			-1, &old_stmt, NULL) != SQLITE_OK){
		sqlite3_finalize(new_stmt);
		sqlite3_close(db);
		db = old_db;
		return 1;
	}
	while(sqlite3_step(old_stmt) == SQLITE_ROW){
		client_id = (const char *)sqlite3_column_text(old_stmt, 0);
		if(sqlite3_bind_text(new_stmt, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		topic = (const char *)sqlite3_column_text(old_stmt, 1);
		if(sqlite3_bind_text(new_stmt, 2, topic, strlen(topic), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(new_stmt, 3, sqlite3_column_int(old_stmt, 2)) != SQLITE_OK) rc = 1;
		if(sqlite3_step(new_stmt) != SQLITE_DONE) rc = 1;
		sqlite3_reset(new_stmt);
		sqlite3_clear_bindings(new_stmt);
	}
	sqlite3_finalize(new_stmt);
	sqlite3_finalize(old_stmt);

	/* ---------- Copy retain messages to message store ---------- */
	if(sqlite3_prepare_v2(db, "REPLACE INTO retain (topic,store_id) VALUES (?,?)",
			-1, &new_stmt, NULL) != SQLITE_OK){
		sqlite3_close(db);
		db = old_db;
		return 1;
	}
	if(sqlite3_prepare_v2(old_db, "SELECT topic,qos,payloadlen,payload FROM retain",
			-1, &old_stmt, NULL) != SQLITE_OK){
		sqlite3_finalize(new_stmt);
		sqlite3_close(db);
		db = old_db;
		return 1;
	}
	while(sqlite3_step(old_stmt) == SQLITE_ROW){
		topic = (const char *)sqlite3_column_text(old_stmt, 0);
		qos = sqlite3_column_int(old_stmt, 1);
		payloadlen = sqlite3_column_int(old_stmt, 2);
		payload = sqlite3_column_blob(old_stmt, 3);

		if(mqtt3_db_message_store("", topic, qos, payloadlen, payload, 1, &store_id)) rc = 1;
		if(sqlite3_bind_text(new_stmt, 1, topic, strlen(topic), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int64(new_stmt, 2, store_id) != SQLITE_OK) rc = 1;
		if(sqlite3_step(new_stmt) != SQLITE_DONE) rc = 1;
		sqlite3_reset(new_stmt);
		sqlite3_clear_bindings(new_stmt);
	}
	sqlite3_finalize(new_stmt);
	sqlite3_finalize(old_stmt);

	/* ---------- Copy messages to message store ---------- */
	if(sqlite3_prepare_v2(db, "INSERT INTO messages (client_id,timestamp,direction,status,mid,retries,qos,store_id) VALUES (?,?,?,?,?,?,?,?)",
			-1, &new_stmt, NULL) != SQLITE_OK){
		sqlite3_close(db);
		db = old_db;
		return 1;
	}
	if(sqlite3_prepare_v2(old_db, "SELECT client_id,timestamp,direction,status,mid,dup,qos,retain,topic,payloadlen,payload FROM messages",
			-1, &old_stmt, NULL) != SQLITE_OK){
		sqlite3_finalize(new_stmt);
		sqlite3_close(db);
		db = old_db;
		return 1;
	}
	while(sqlite3_step(old_stmt) == SQLITE_ROW){
		client_id = (const char *)sqlite3_column_text(old_stmt, 0);
		timestamp = sqlite3_column_int(old_stmt, 1);
		direction = sqlite3_column_int(old_stmt, 2);
		status = sqlite3_column_int(old_stmt, 3);
		mid = sqlite3_column_int(old_stmt, 4);
		dup = sqlite3_column_int(old_stmt, 5);
		qos = sqlite3_column_int(old_stmt, 6);
		retain = sqlite3_column_int(old_stmt, 7);
		topic = (const char *)sqlite3_column_text(old_stmt, 8);
		payloadlen = sqlite3_column_int(old_stmt, 9);
		payload = sqlite3_column_blob(old_stmt, 10);

		if(mqtt3_db_message_store("", topic, qos, payloadlen, payload, 1, &store_id)) rc = 1;
		if(sqlite3_bind_text(new_stmt, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(new_stmt, 2, timestamp) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(new_stmt, 3, direction) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(new_stmt, 4, status) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(new_stmt, 5, mid) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(new_stmt, 6, dup) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(new_stmt, 7, timestamp) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int64(new_stmt, 8, store_id) != SQLITE_OK) rc = 1;
		if(sqlite3_step(new_stmt) != SQLITE_DONE) rc = 1;
		sqlite3_reset(new_stmt);
		sqlite3_clear_bindings(new_stmt);
	}
	sqlite3_finalize(new_stmt);
	sqlite3_finalize(old_stmt);

	_mqtt3_db_statements_finalize(old_db);
	sqlite3_close(old_db);
	mqtt3_db_backup(false);

	return rc;
}
#endif

/* Internal function.
 * Finalise all sqlite statements bound to fdb. This must be done before
 * closing the db.
 * See also _mqtt3_db_statement_prepare().
 */
static void _mqtt3_db_statements_finalize(sqlite3 *fdb)
{
	sqlite3_stmt *stmt;

	while((stmt = sqlite3_next_stmt(fdb, NULL))){
		sqlite3_finalize(stmt);
	}
}

/* Called on client death to add a will to the message queue if the will exists.
 * Returns 1 on failure (context or context->core.id is NULL, sqlite error)
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
 * Returns 1 on failure (count is NULL, sqlite error)
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
 * Returns 1 on failure (sqlite error)
 * Returns 0 on success.
 */
static int _mqtt3_db_cleanup(void)
{
	int rc = 0;
	char *query = NULL;
	char *errmsg;

	if(!db) return 1;

#if 0
// FIXME - reimplement for new db
	query = sqlite3_mprintf("UPDATE clients SET sock=-1");
	if(query){
		if(sqlite3_exec(db, query, NULL, NULL, &errmsg) != SQLITE_OK){
			rc = 1;
		}
		sqlite3_free(query);
		if(errmsg){
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s", errmsg);
			sqlite3_free(errmsg);
		}
	}else{
		return 1;
	}

	/* Remove any stray clients that have clean session set. */
	query = sqlite3_mprintf("DELETE FROM clients WHERE sock=-1 AND clean_session=1");
	if(query){
		if(sqlite3_exec(db, query, NULL, NULL, &errmsg) != SQLITE_OK){
			rc = 1;
		}
		sqlite3_free(query);
		if(errmsg){
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s", errmsg);
			sqlite3_free(errmsg);
		}
	}else{
		return 1;
	}

	/* Remove any subs with no client. */
	query = sqlite3_mprintf("DELETE FROM subs WHERE client_id NOT IN (SELECT id FROM clients)");
	if(query){
		if(sqlite3_exec(db, query, NULL, NULL, &errmsg) != SQLITE_OK){
			rc = 1;
		}
		sqlite3_free(query);
		if(errmsg){
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s", errmsg);
			sqlite3_free(errmsg);
		}
	}else{
		return 1;
	}

	/* Remove any messages with no client. */
	query = sqlite3_mprintf("DELETE FROM messages WHERE client_id NOT IN (SELECT id FROM clients)");
	if(query){
		if(sqlite3_exec(db, query, NULL, NULL, &errmsg) != SQLITE_OK){
			rc = 1;
		}
		sqlite3_free(query);
		if(errmsg){
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s", errmsg);
			sqlite3_free(errmsg);
		}
	}else{
		return 1;
	}
#endif
	return rc;
}

int mqtt3_db_message_delete(mqtt3_context *context, uint16_t mid, enum mosquitto_msg_direction dir)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!context) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("DELETE FROM messages WHERE client_id=? AND mid=? AND direction=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, context->core.id, strlen(context->core.id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 2, mid) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 3, dir) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

int mqtt3_db_message_delete_by_oid(int64_t oid)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("DELETE FROM messages WHERE OID=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_int64(stmt, 1, oid) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

int mqtt3_db_message_insert(mqtt3_context *context, uint16_t mid, enum mosquitto_msg_direction dir, enum mqtt3_msg_status status, int qos, struct mosquitto_msg_store *stored)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;
	static sqlite3_stmt *count_stmt = NULL;
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
	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("INSERT INTO messages "
				"(client_id, timestamp, direction, status, mid, retries, qos, store_id) "
				"VALUES (?,?,?,?,?,0,?,?)");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, context->core.id, strlen(context->core.id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 2, time(NULL)) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 3, dir) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 4, status) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 5, mid) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 6, qos) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int64(stmt, 7, 1 /* FIXME */) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

int mqtt3_db_message_update(mqtt3_context *context, uint16_t mid, enum mosquitto_msg_direction dir, enum mqtt3_msg_status status)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	/* This will only ever get called for messages with QoS>0 so mid must not be 0 */
	if(!context || !mid) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("UPDATE messages SET status=?,timestamp=? "
				"WHERE client_id=? AND mid=? AND direction=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_int(stmt, 1, status) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 2, time(NULL)) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_text(stmt, 3, context->core.id, strlen(context->core.id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 4, mid) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 5, dir) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

int mqtt3_db_messages_delete(mqtt3_context *context)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!context) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("DELETE FROM messages WHERE client_id=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, context->core.id, strlen(context->core.id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
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

	return mqtt3_db_messages_queue(db, context->core.id, topic, qos, retain, stored);
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
	static sqlite3_stmt *stmt = NULL;
	int rc = 0;
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
		temp->msg.payload = malloc(sizeof(uint8_t)*payloadlen);
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

int mqtt3_db_message_timeout_check(unsigned int timeout)
{
	int rc = 0;
	time_t now = time(NULL) - timeout;
	static sqlite3_stmt *stmt_select = NULL;
	static sqlite3_stmt *stmt_update = NULL;
	int64_t OID;
	int status;
	int retries;
	enum mqtt3_msg_status new_status = ms_invalid;

	if(!stmt_select){
		stmt_select = _mqtt3_db_statement_prepare("SELECT OID,status,retries FROM messages WHERE timestamp<?");
		if(!stmt_select){
			return 1;
		}
	}
	if(!stmt_update){
		stmt_update = _mqtt3_db_statement_prepare("UPDATE messages SET status=?,retries=? WHERE OID=?");
		if(!stmt_update){
			return 1;
		}
	}
	if(sqlite3_bind_int(stmt_select, 1, now) != SQLITE_OK) rc = 1;
	while(sqlite3_step(stmt_select) == SQLITE_ROW){
		OID = sqlite3_column_int64(stmt_select, 0);
		status = sqlite3_column_int(stmt_select, 1);
		retries = sqlite3_column_int(stmt_select, 2) + 1;
		switch(status){
			case ms_wait_puback:
				new_status = ms_publish_puback;
				break;
			case ms_wait_pubrec:
				new_status = ms_publish_pubrec;
				break;
			case ms_wait_pubrel:
				new_status = ms_resend_pubrel;
				break;
			case ms_wait_pubcomp:
				new_status = ms_resend_pubcomp;
				break;
		}
		if(new_status != ms_invalid){
			if(sqlite3_bind_int(stmt_update, 1, new_status) != SQLITE_OK) rc = 1;
			if(sqlite3_bind_int(stmt_update, 2, retries) != SQLITE_OK) rc = 1;
			if(sqlite3_bind_int64(stmt_update, 3, OID) != SQLITE_OK) rc = 1;
			if(sqlite3_step(stmt_update) != SQLITE_DONE) rc = 1;
			sqlite3_reset(stmt_update);
			sqlite3_clear_bindings(stmt_update);
		}
	}
	sqlite3_reset(stmt_select);
	sqlite3_clear_bindings(stmt_select);
	return MOSQ_ERR_SUCCESS;
}

int mqtt3_db_message_release(mosquitto_db *db, mqtt3_context *context, uint16_t mid, enum mosquitto_msg_direction dir)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;
	int64_t OID;
	int qos;
	int retain;
	int64_t store_id;
	char *topic;
	char *source_id;

	if(!context) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("SELECT messages.OID,message_store.id,message_store.qos,message_store.retain,message_store.topic,message_store.source_id "
				"FROM messages JOIN message_store on messages.store_id=message_store.id "
				"WHERE messages.client_id=? AND messages.mid=? AND messages.direction=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, context->core.id, strlen(context->core.id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 2, mid) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 3, dir) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) == SQLITE_ROW){
		OID = sqlite3_column_int64(stmt, 0);
		store_id = sqlite3_column_int64(stmt, 1);
		qos = sqlite3_column_int(stmt, 2);
		retain = sqlite3_column_int(stmt, 3);
		topic = (char *)sqlite3_column_text(stmt, 4);
		source_id = (char *)sqlite3_column_text(stmt, 5);
		if(!mqtt3_db_messages_queue(db, source_id, topic, qos, retain, NULL /* FIXME */)){
			if(mqtt3_db_message_delete_by_oid(OID)) rc = 1;
		}else{
			rc = 1;
		}
	}else{
		rc = 1;
	}
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	return rc;
}

int mqtt3_db_message_write(mqtt3_context *context)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;
	int64_t OID;
	int status;
	uint16_t mid;
	int retries;
	int retain;
	const char *topic;
	int qos;
	uint32_t payloadlen;
	const uint8_t *payload;

	if(!context || !context->core.id || context->core.sock == -1) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("SELECT messages.OID,messages.status,messages.mid,"
				"messages.retries,message_store.retain,message_store.topic,messages.qos,"
				"message_store.payloadlen,message_store.payload "
				"FROM messages JOIN message_store ON messages.store_id=message_store.id "
				"WHERE status IN (1, 2, 4, 6, 8) "
				"AND direction=1 AND client_id=? ORDER BY message_store.timestamp");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, context->core.id, strlen(context->core.id), SQLITE_STATIC) == SQLITE_OK){
		while(sqlite3_step(stmt) == SQLITE_ROW){
			OID = sqlite3_column_int64(stmt, 0);
			status = sqlite3_column_int(stmt, 1);
			mid = sqlite3_column_int(stmt, 2);
			retries = sqlite3_column_int(stmt, 3);
			retain = sqlite3_column_int(stmt, 4);
			topic = (const char *)sqlite3_column_text(stmt, 5);
			qos = sqlite3_column_int(stmt, 6);
			payloadlen = sqlite3_column_int(stmt, 7);
			if(payloadlen){
				payload = sqlite3_column_blob(stmt, 8);
			}else{
				payload = NULL;
			}
			switch(status){
				case ms_publish:
					if(!mqtt3_raw_publish(context, retries, qos, retain, mid, topic, payloadlen, payload)){
						mqtt3_db_message_delete_by_oid(OID);
					}
					break;

				case ms_publish_puback:
					if(!mqtt3_raw_publish(context, retries, qos, retain, mid, topic, payloadlen, payload)){
						mqtt3_db_message_update(context, mid, mosq_md_out, ms_wait_puback);
					}
					break;

				case ms_publish_pubrec:
					if(!mqtt3_raw_publish(context, retries, qos, retain, mid, topic, payloadlen, payload)){
						mqtt3_db_message_update(context, mid, mosq_md_out, ms_wait_pubrec);
					}
					break;
				
				case ms_resend_pubrel:
					if(!mqtt3_raw_pubrel(context, mid)){
						mqtt3_db_message_update(context, mid, mosq_md_out, ms_wait_pubrel);
					}
					break;

				case ms_resend_pubcomp:
					if(!mqtt3_raw_pubcomp(context, mid)){
						mqtt3_db_message_update(context, mid, mosq_md_out, ms_wait_pubcomp);
					}
					break;
			}
		}
	}else{
		rc = 1;
	}
	sqlite3_reset(stmt);

	return rc;
}

int mqtt3_db_retain_queue(mqtt3_context *context, const char *sub, int sub_qos)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;
	const char *topic;
	int qos;
	struct mosquitto_msg_store *stored = NULL;
	uint16_t mid;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("SELECT message_store.topic,qos,id FROM message_store "
				"JOIN retain ON message_store.id=retain.store_id WHERE retain.topic=?");
		if(!stmt) return 1;
	}
	if(sqlite3_bind_text(stmt, 1, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;

	while(sqlite3_step(stmt) == SQLITE_ROW){
		topic = (const char *)sqlite3_column_text(stmt, 0);
		qos = sqlite3_column_int(stmt, 1);

		if(qos > sub_qos) qos = sub_qos;
		if(qos > 0){
			mid = _mosquitto_mid_generate(&context->core);
		}else{
			mid = 0;
		}
		switch(qos){
			case 0:
				if(mqtt3_db_message_insert(context, mid, mosq_md_out, ms_publish, qos, stored) == 1) rc = 1;
				break;
			case 1:
				if(mqtt3_db_message_insert(context, mid, mosq_md_out, ms_publish_puback, qos, stored) == 1) rc = 1;
				break;
			case 2:
				if(mqtt3_db_message_insert(context, mid, mosq_md_out, ms_publish_pubrec, qos, stored) == 1) rc = 1;
				break;
		}
	}
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	return rc;
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
		snprintf(buf, 100, "%lld", _mosquitto_memory_used()+sqlite3_memory_used());
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

/* Internal function.
 * Prepare a sqlite query.
 * All of the regularly used sqlite queries are prepared as parameterised
 * queries so they only need to be parsed once and to increase security by
 * removing the need to carry out string escaping.
 * Before closing the database, all currently prepared queries must be released.
 * To do this, each function that needs to make a query has a static
 * sqlite3_stmt variable to hold the query statement. _m_d_s_p() prepares the
 * statement and the function stores it in its static variable. _m_d_s_p() also
 * adds the statement to a global static array so that the statements can be
 * released when mosquitto is closing.
 *
 * Returns NULL on failure
 * Returns a valid sqlite3_stmt on success.
 */
static sqlite3_stmt *_mqtt3_db_statement_prepare(const char *query)
{
	sqlite3_stmt *stmt;

	if(sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK){
		return NULL;
	}
	return stmt;
}

void mqtt3_db_limits_set(int inflight, int queued)
{
	max_inflight = inflight;
	max_queued = queued;
}

void mqtt3_db_vacuum(void)
{
	char *errmsg = NULL;
	sqlite3_exec(db, "VACUUM", NULL, NULL, &errmsg);
	if(errmsg){
		sqlite3_free(errmsg);
	}
}

