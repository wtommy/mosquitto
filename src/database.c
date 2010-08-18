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
 *
 * When a message is ready to be published at the broker, we need to check all
 * of the subscriptions to see which ones the message should be sent to. This
 * would be easy without wildcards, but requires a bit more work with them.
 *
 * The regex used to do the matching is of the form below for a topic of a/b/c:
 *
 * ^(?:(?:(a|\+)(?!$))(?:(?:/(?:(b|\+)(?!$)))(?:(?:/(?:c|\+))|/#)?|/#)?|#)$
 *
 * In general, we're matching (a or +) followed by (the next levels of
 * hierarchy or #).
 * More specifically, all the levels of hierarchy must match, unless the last
 * level is #.
 *
 * ^(?:							# Must start at beginning of string
 * 		(?:						# (Level 1 hierarchy)
 * 			(a|\+)(?!$) 		# Match a or +, but only if not EOL.
 * 		)						# AND 
 * 		(?:
 * 			(?:					# (Level 2 hierarchy)
 * 				/				# Match /
 * 				(?:				# AND
 * 					(b|\+)(?!$)	# Match b or +, but only if not EOL.
 * 				)
 * 			)					# AND
 * 			(?:
 * 				(?:				# (Level 3 hierarchy)
 * 					/			# Match /
 * 					(?:			# AND
 * 						c|\+	# Match c or +.
 * 					)
 * 				)
 * 				|				# OR (instead of level 3)
 * 				/#				# Match /# at level 3
 * 			)?					# Level 3 exist 1/0 times
 * 			|					# OR (instead of level 2)
 * 			/#					# Match /# at level 2
 * 		)?						# Level 2 exist 1/0 times
 * 		|						# OR (instead of level 1)
 * 		#						# Match # at level 1
 * 	)$							# Must end on EOL.
 *
 *
 */

#include <errno.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

#include <config.h>
#include <mqtt3.h>
#include <memory_mosq.h>

static sqlite3 *db = NULL;
static char *db_filepath = NULL;

static sqlite3_stmt *stmt_sub_search = NULL;
static int max_inflight = 20;
static int max_queued = 100;

static int _mqtt3_db_tables_create(void);
static int _mqtt3_db_cleanup(void);
#ifdef WITH_REGEX
static int _mqtt3_db_regex_create(const char *topic, char **regex);
static int _mqtt3_db_retain_regex_create(const char *sub, char **regex);
#endif
static sqlite3_stmt *_mqtt3_db_statement_prepare(const char *query);
static void _mqtt3_db_statements_finalize(sqlite3 *fdb);
static int _mqtt3_db_version_check(void);
static int _mqtt3_db_transaction_begin(void);
static int _mqtt3_db_transaction_end(void);
#if defined(WITH_BROKER) && defined(WITH_DB_UPGRADE)
static int _mqtt3_db_upgrade(void);
static int _mqtt3_db_upgrade_1_2(void);
#endif
#if 0
static int _mqtt3_db_transaction_rollback(void);
#endif

int mqtt3_db_open(mqtt3_config *config)
{
#ifdef WITH_REGEX
	char *errmsg = NULL;
#endif
	int dbrc;
	int rc = 0;
	sqlite3_backup *restore;
	sqlite3 *restore_db;
	FILE *fptr;

	if(!config) return 1;
#ifdef WITH_REGEX
	if(!config->ext_sqlite_regex) return 1;
#endif
	if(sqlite3_initialize() != SQLITE_OK){
		return 1;
	}

	if(sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK){
		mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s", sqlite3_errmsg(db));
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
						mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Unable to upgrade database.");
						return 1;
					}
#else
					mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Invalid database version.");
					return 1;
#endif
				}
			}else{
				mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Couldn't restore database %s (%d).", db_filepath, dbrc);
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
							mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Unable to populate new database. Try restarting mosquitto.");
							return 1;
						}
						break;
					case EACCES:
						mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Permission denied trying to restore persistent database %s.", db_filepath);
						return 1;
					default:
						mqtt3_log_printf(MQTT3_LOG_ERR, "%s", strerror(errno));
						return 1;
				}
			}else{
				mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Possibly corrupt database file. Try restarting mosquitto.");
				fclose(fptr);
			}
		}
	}

#ifdef WITH_REGEX
	sqlite3_enable_load_extension(db, 1);
	if(sqlite3_load_extension(db, config->ext_sqlite_regex, NULL, &errmsg) != SQLITE_OK){
		if(errmsg){
			mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s", errmsg);
			sqlite3_free(errmsg);
		}
		return 1;
	}
#endif
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

	return 0;
}

int mqtt3_db_backup(bool cleanup)
{
	char *errmsg = NULL;
	int rc = 0;
	sqlite3 *backup_db;
	sqlite3_backup *backup;

	if(!db || !db_filepath) return 1;
	mqtt3_log_printf(MQTT3_LOG_INFO, "Saving in-memory database to %s.", db_filepath);
	if(cleanup){
		mqtt3_db_store_clean();
	}
	if(sqlite3_open(db_filepath, &backup_db) != SQLITE_OK){
		mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Unable to open on-disk database for writing.");
		return 1;
	}
	backup = sqlite3_backup_init(backup_db, "main", db, "main");
	if(backup){
		sqlite3_backup_step(backup, -1);
		sqlite3_backup_finish(backup);
	}else{
		mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Unable to save in-memory database to disk.");
		rc = 1;
	}
	if(cleanup){
		sqlite3_exec(backup_db, "VACUUM", NULL, NULL, &errmsg);
		if(errmsg){
			sqlite3_free(errmsg);
		}
	}
	sqlite3_close(backup_db);
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
		"CREATE TABLE IF NOT EXISTS clients("
		"sock INTEGER, "
		"id TEXT PRIMARY KEY, "
		"clean_session INTEGER, "
		"will INTEGER, will_retain INTEGER, will_qos INTEGER, "
		"will_topic TEXT, will_message TEXT, "
		"last_mid INTEGER, "
		"is_bridge INTEGER)",
		NULL, NULL, &errmsg) != SQLITE_OK){

		rc = 1;
	}
	if(errmsg){
		sqlite3_free(errmsg);
		errmsg = NULL;
	}

	if(sqlite3_exec(db,
		"CREATE TABLE IF NOT EXISTS subs("
		"client_id TEXT, sub TEXT, qos INTEGER)",
		NULL, NULL, &errmsg) != SQLITE_OK){

		rc = 1;
	}
	if(errmsg){
		sqlite3_free(errmsg);
		errmsg = NULL;
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
				mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Upgrading from DB version 0 not supported.");
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
	
	mqtt3_log_printf(MQTT3_LOG_NOTICE, "Upgrading database from version 1 to 2.");

	if(sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK){
		mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s", sqlite3_errmsg(db));
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

/* Adds a new client to the database.
 * This should be called when a new connection has successfully sent a CONNECT command.
 * If a client is already connected with the same id, the old client will be
 * disconnected and the information updated.
 * If will=1 then a will will be stored for the client. In this case,
 * will_topic and will_message must not be NULL otherwise this will return
 * failure.
 * Returns 1 on failure (context or context->id is NULL, sqlite error)
 * Returns 0 on success.
 */
int mqtt3_db_client_insert(mqtt3_context *context, int will, int will_retain, int will_qos, const char *will_topic, const char *will_message)
{
	static sqlite3_stmt *stmt = NULL;
	int rc = 0;
	int oldsock;

	if(!context || !context->id) return 1;
	if(will && (!will_topic || !will_message)) return 1;

	if(!mqtt3_db_client_find_socket(context->id, &oldsock)){
		if(oldsock == -1){
			/* Client is reconnecting after a disconnect */
		}else if(oldsock != context->sock){
			/* Client is already connected, disconnect old version */
			mqtt3_log_printf(MQTT3_LOG_ERR, "Client %s already connected, closing old connection.", context->id);
#ifdef WITH_BROKER
			mqtt3_context_close_duplicate(oldsock);
#else
			close(oldsock);
#endif
		}
		mqtt3_db_client_update(context, will, will_retain, will_qos, will_topic, will_message);
	}else{
		if(!stmt){
			stmt = _mqtt3_db_statement_prepare("INSERT INTO clients "
					"(sock,id,clean_session,will,will_retain,will_qos,will_topic,will_message,last_mid,is_bridge) "
					"SELECT ?,?,?,?,?,?,?,?,0,? WHERE NOT EXISTS "
					"(SELECT 1 FROM clients WHERE id=?)");
			if(!stmt){
				return 1;
			}
		}
		if(sqlite3_bind_int(stmt, 1, context->sock) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_text(stmt, 2, context->id, strlen(context->id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(stmt, 3, context->clean_session) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(stmt, 4, will) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(stmt, 5, will_retain) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(stmt, 6, will_qos) != SQLITE_OK) rc = 1;
		if(will_topic){
			if(sqlite3_bind_text(stmt, 7, will_topic, strlen(will_topic), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		}else{
			if(sqlite3_bind_text(stmt, 7, "", 0, SQLITE_STATIC) != SQLITE_OK) rc = 1;
		}
		if(will_message){
			if(sqlite3_bind_text(stmt, 8, will_message, strlen(will_message), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		}else{
			if(sqlite3_bind_text(stmt, 8, "", 0, SQLITE_STATIC) != SQLITE_OK) rc = 1;
		}
		if(sqlite3_bind_int(stmt, 9, (context->bridge)?1:0) != SQLITE_OK) rc = 1;
		if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
		sqlite3_reset(stmt);
		sqlite3_clear_bindings(stmt);
	}
	return rc;
}

/* Update a client connection in the database.
 * This will be called if a client with the same id connects twice (see
 * mqtt3_db_client_insert()), or if a client reconnects that had clean start
 * disabled.
 * If will=1 then a will will be stored for the client. In this case,
 * will_topic and will_message must not be NULL otherwise this will return
 * failure.
 * Returns 1 on failure (context or context->id is NULL, sqlite error)
 * Returns 0 on success.
 */
int mqtt3_db_client_update(mqtt3_context *context, int will, int will_retain, int will_qos, const char *will_topic, const char *will_message)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!context || !context->id) return 1;
	if(will && (!will_topic || !will_message)) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("UPDATE clients SET "
				"sock=?,clean_session=?,will=?,will_retain=?,will_qos=?,"
				"will_topic=?,will_message=? WHERE id=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_int(stmt, 1, context->sock) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 2, context->clean_session) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 3, will) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 4, will_retain) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 5, will_qos) != SQLITE_OK) rc = 1;
	if(will_topic){
		if(sqlite3_bind_text(stmt, 6, will_topic, strlen(will_topic), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	}else{
		if(sqlite3_bind_text(stmt, 6, "", 0, SQLITE_STATIC) != SQLITE_OK) rc = 1;
	}
	if(will_message){
		if(sqlite3_bind_text(stmt, 7, will_message, strlen(will_message), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	}else{
		if(sqlite3_bind_text(stmt, 7, "", 0, SQLITE_STATIC) != SQLITE_OK) rc = 1;
	}
	if(sqlite3_bind_text(stmt, 8, context->id, strlen(context->id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

/* Called on client death to add a will to the message queue if the will exists.
 * Returns 1 on failure (context or context->id is NULL, sqlite error)
 * Returns 0 on success (will queued or will not found)
 */
int mqtt3_db_client_will_queue(mqtt3_context *context)
{
	int rc = 0;
	int dbrc;
	static sqlite3_stmt *stmt = NULL;
	const char *topic;
	int qos;
	const uint8_t *payload;
	int retain;
	int will;

	if(!context || !context->id) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("SELECT will,will_topic,will_qos,will_message,will_retain FROM clients WHERE id=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, context->id, strlen(context->id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	dbrc = sqlite3_step(stmt);
	if(dbrc == SQLITE_ROW){
		will = sqlite3_column_int(stmt, 0);
		if(!will){
			sqlite3_reset(stmt);
			sqlite3_clear_bindings(stmt);
			return 0;
		}
		topic = (const char *)sqlite3_column_text(stmt, 1);
		qos = sqlite3_column_int(stmt, 2);
		payload = sqlite3_column_text(stmt, 3);
		retain = sqlite3_column_int(stmt, 4);
		if(!rc){
			if(mqtt3_db_messages_easy_queue(context->id, topic, qos, strlen((const char *)payload), payload, retain)) rc = 1;
		}
	}else if(dbrc != SQLITE_DONE){
		rc = 1;
	}
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

/* Returns the number of client currently in the database.
 * This includes inactive clients.
 * Returns 1 on failure (count is NULL, sqlite error)
 * Returns 0 on success.
 */
int mqtt3_db_client_count(int *count)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!count) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("SELECT COUNT(*) FROM clients");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_step(stmt) == SQLITE_ROW){
		*count = sqlite3_column_int(stmt, 0);
	}else{
		rc = 1;
	}
	sqlite3_reset(stmt);

	return rc;
}

/* Delete a client from the database.
 * Called when clients with clean start enabled disconnect.
 * Returns 1 on failure (context or context->id is NULL, sqlite error)
 * Returns 0 on success.
 */
int mqtt3_db_client_delete(mqtt3_context *context)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!context || !(context->id)) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("DELETE FROM clients WHERE id=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, context->id, strlen(context->id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

/* Find the socket for given client id.
 * Returns 1 on failure (client_id or sock is NULL, client id not found)
 * Returns 0 on success.
 */
int mqtt3_db_client_find_socket(const char *client_id, int *sock)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!client_id || !sock) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("SELECT sock FROM clients WHERE id=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) == SQLITE_ROW){
		*sock = sqlite3_column_int(stmt, 0);
	}else{
		rc = 1;
	}
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
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

	query = sqlite3_mprintf("UPDATE clients SET sock=-1");
	if(query){
		if(sqlite3_exec(db, query, NULL, NULL, &errmsg) != SQLITE_OK){
			rc = 1;
		}
		sqlite3_free(query);
		if(errmsg){
			mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s", errmsg);
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
			mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s", errmsg);
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
			mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s", errmsg);
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
			mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s", errmsg);
			sqlite3_free(errmsg);
		}
	}else{
		return 1;
	}

	return rc;
}

/* Sets stored socket for a given client to -1.
 * Called when a client with clean start disabled disconnects and hence has no
 * associated socket.
 * Returns 1 on failure (client_id is NULL, sqlite error)
 * Returns 0 on success.
 */
int mqtt3_db_client_invalidate_socket(const char *client_id, int sock)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!db || !client_id) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("UPDATE clients SET sock=-1 WHERE id=? AND sock=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 2, sock) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

/* Returns the number of messages currently in the database - this is the
 * number of messages in flight and doesn't include retained messages.
 * FIXME - this probably needs updating to consider messages in the store.
 * Returns 1 on failure (count is NULL, sqlite error)
 * Returns 0 on success.
 */
int mqtt3_db_message_count(int *count)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!count) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("SELECT COUNT(client_id) FROM messages");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_step(stmt) == SQLITE_ROW){
		*count = sqlite3_column_int(stmt, 0);
	}else{
		rc = 1;
	}
	sqlite3_reset(stmt);

	return rc;
}

int mqtt3_db_message_delete(const char *client_id, uint16_t mid, enum mosquitto_msg_direction dir)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!client_id) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("DELETE FROM messages WHERE client_id=? AND mid=? AND direction=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
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

int mqtt3_db_message_insert(const char *client_id, uint16_t mid, enum mosquitto_msg_direction dir, enum mqtt3_msg_status status, int qos, int64_t store_id)
{
	/* Warning: Don't start transaction in this function. */
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;
	static sqlite3_stmt *count_stmt = NULL;
	int count = 0;
	int sock = -1;
	int limited = 0;

	if(!client_id || !store_id) return 1;

	if(!count_stmt){
		count_stmt = _mqtt3_db_statement_prepare("SELECT "
				"(SELECT COUNT(*) FROM messages WHERE client_id=?),"
				"(SELECT sock FROM clients WHERE id=?)");
		if(!count_stmt){
			return 1;
		}
	}
	if(max_inflight || max_queued){
		if(sqlite3_bind_text(count_stmt, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_text(count_stmt, 2, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
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

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("INSERT INTO messages "
				"(client_id, timestamp, direction, status, mid, retries, qos, store_id) "
				"VALUES (?,?,?,?,?,0,?,?)");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 2, time(NULL)) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 3, dir) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 4, status) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 5, mid) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 6, qos) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int64(stmt, 7, store_id) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

int mqtt3_db_message_update(const char *client_id, uint16_t mid, enum mosquitto_msg_direction dir, enum mqtt3_msg_status status)
{
	/* Warning: Don't start transaction in this function. */
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	/* This will only ever get called for messages with QoS>0 so mid must not be 0 */
	if(!client_id || !mid) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("UPDATE messages SET status=?,timestamp=? "
				"WHERE client_id=? AND mid=? AND direction=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_int(stmt, 1, status) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 2, time(NULL)) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_text(stmt, 3, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 4, mid) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 5, dir) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

int mqtt3_db_messages_delete(const char *client_id)
{
	/* Warning: Don't start transaction in this function. */
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!client_id) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("DELETE FROM messages WHERE client_id=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

int mqtt3_db_messages_easy_queue(const char *client_id, const char *topic, int qos, uint32_t payloadlen, const uint8_t *payload, int retain)
{
	int64_t store_id;

	if(!topic) return 1;

	if(mqtt3_db_message_store(client_id, topic, qos, payloadlen, payload, retain, &store_id)) return 1;

	return mqtt3_db_messages_queue(client_id, topic, qos, retain, store_id);
}

int mqtt3_db_messages_queue(const char *source_id, const char *topic, int qos, int retain, int64_t store_id)
{
	/* Warning: Don't start transaction in this function. */
	int rc = 0;
	char *client_id;
	uint8_t client_qos;
	uint8_t msg_qos;
	uint16_t mid;

	/* Find all clients that subscribe to topic and put messages into the db for them. */
	if(!source_id || !topic || !store_id) return 1;

	if(retain){
		if(mqtt3_db_retain_insert(topic, store_id)) rc = 1;
	}
	if(!mqtt3_db_sub_search_start(source_id, topic, qos)){
		while(sqlite3_step(stmt_sub_search) == SQLITE_ROW){
			client_id = (char *)sqlite3_column_text(stmt_sub_search, 0);
			client_qos = sqlite3_column_int(stmt_sub_search, 1);
			if(qos > client_qos){
				msg_qos = client_qos;
			}else{
				msg_qos = qos;
			}
			if(msg_qos){
				mid = mqtt3_db_mid_generate(client_id);
			}else{
				mid = 0;
			}
			switch(msg_qos){
				case 0:
					if(mqtt3_db_message_insert(client_id, mid, mosq_md_out, ms_publish, msg_qos, store_id) == 1) rc = 1;
					break;
				case 1:
					if(mqtt3_db_message_insert(client_id, mid, mosq_md_out, ms_publish_puback, msg_qos, store_id) == 1) rc = 1;
					break;
				case 2:
					if(mqtt3_db_message_insert(client_id, mid, mosq_md_out, ms_publish_pubrec, msg_qos, store_id) == 1) rc = 1;
					break;
			}
		}
	}
	sqlite3_reset(stmt_sub_search);
	sqlite3_clear_bindings(stmt_sub_search);
	return rc;
}

int mqtt3_db_message_store(const char *client_id, const char *topic, int qos, uint32_t payloadlen, const uint8_t *payload, int retain, int64_t *store_id)
{
	/* Warning: Don't start transaction in this function. */
	static sqlite3_stmt *stmt = NULL;
	int rc = 0;

	if(!client_id || !topic || !store_id) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("INSERT INTO message_store "
				"(timestamp, qos, retain, topic, payloadlen, payload, source_id) "
				"VALUES (?,?,?,?,?,?,?)");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_int(stmt, 1, time(NULL)) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 2, qos) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 3, retain) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_text(stmt, 4, topic, strlen(topic), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 5, payloadlen) != SQLITE_OK) rc = 1;
	if(payloadlen){
		if(sqlite3_bind_blob(stmt, 6, payload, payloadlen, SQLITE_STATIC) != SQLITE_OK) rc = 1;
	}
	if(sqlite3_bind_text(stmt, 7, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	if(rc) return 1;
	*store_id = sqlite3_last_insert_rowid(db);

	return rc;
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
	_mqtt3_db_transaction_begin();
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
	_mqtt3_db_transaction_end();
	return 0;
}

int mqtt3_db_message_release(const char *client_id, uint16_t mid, enum mosquitto_msg_direction dir)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;
	int64_t OID;
	int qos;
	int retain;
	int64_t store_id;
	char *topic;
	char *source_id;

	if(!client_id) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("SELECT messages.OID,message_store.id,message_store.qos,message_store.retain,message_store.topic,message_store.source_id "
				"FROM messages JOIN message_store on messages.store_id=message_store.id "
				"WHERE messages.client_id=? AND messages.mid=? AND messages.direction=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 2, mid) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 3, dir) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) == SQLITE_ROW){
		OID = sqlite3_column_int64(stmt, 0);
		store_id = sqlite3_column_int64(stmt, 1);
		qos = sqlite3_column_int(stmt, 2);
		retain = sqlite3_column_int(stmt, 3);
		topic = (char *)sqlite3_column_text(stmt, 4);
		source_id = (char *)sqlite3_column_text(stmt, 5);
		if(!mqtt3_db_messages_queue(source_id, topic, qos, retain, store_id)){
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

	if(!context || !context->id || context->sock == -1) return 1;

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
	if(sqlite3_bind_text(stmt, 1, context->id, strlen(context->id), SQLITE_STATIC) == SQLITE_OK){
		_mqtt3_db_transaction_begin();
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
						mqtt3_db_message_update(context->id, mid, mosq_md_out, ms_wait_puback);
					}
					break;

				case ms_publish_pubrec:
					if(!mqtt3_raw_publish(context, retries, qos, retain, mid, topic, payloadlen, payload)){
						mqtt3_db_message_update(context->id, mid, mosq_md_out, ms_wait_pubrec);
					}
					break;
				
				case ms_resend_pubrel:
					if(!mqtt3_raw_pubrel(context, mid)){
						mqtt3_db_message_update(context->id, mid, mosq_md_out, ms_wait_pubrel);
					}
					break;

				case ms_resend_pubcomp:
					if(!mqtt3_raw_pubcomp(context, mid)){
						mqtt3_db_message_update(context->id, mid, mosq_md_out, ms_wait_pubcomp);
					}
					break;
			}
		}
		_mqtt3_db_transaction_end();
	}else{
		rc = 1;
	}
	sqlite3_reset(stmt);

	return rc;
}

/* Generate the next message id for a particular client.
 * This is last_mid+1. last_mid remains persistent across connections if clean start is not enabled.
 * Returns 1 on failure (client_id is NULL, sqlite error)
 * Returns new message id on success.
 */
uint16_t mqtt3_db_mid_generate(const char *client_id)
{
	/* Warning: Don't start transaction in this function. */
	int rc = 0;
	static sqlite3_stmt *stmt_select = NULL;
	static sqlite3_stmt *stmt_update = NULL;
	uint16_t mid = 0;

	if(!client_id) return 1;

	if(!stmt_select){
		stmt_select = _mqtt3_db_statement_prepare("SELECT last_mid FROM clients WHERE id=?");
		if(!stmt_select){
			return 1;
		}
	}
	if(!stmt_update){
		stmt_update = _mqtt3_db_statement_prepare("UPDATE clients SET last_mid=? WHERE id=?");
		if(!stmt_update){
			return 1;
		}
	}

	if(sqlite3_bind_text(stmt_select, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt_select) == SQLITE_ROW){
		mid = sqlite3_column_int(stmt_select, 0);
		if(mid == 65535) mid = 0;
		mid++;

		if(sqlite3_bind_int(stmt_update, 1, mid) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_text(stmt_update, 2, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_step(stmt_update) != SQLITE_DONE) rc = 1;
		sqlite3_reset(stmt_update);
		sqlite3_clear_bindings(stmt_update);
	}else{
		rc = 1;
	}
	sqlite3_reset(stmt_select);
	sqlite3_clear_bindings(stmt_select);

	if(!rc){
		return mid;
	}else{
		return 1;
	}
}

#ifdef WITH_REGEX
/* Internal function.
 * Create a regular expression based on 'topic' to match all subscriptions with
 * wildcards + and #.
 * Returns 1 on failure (topic or regex are NULL, out of memory).
 * Regular expression is returned in 'regex'. This memory must not be freed by
 * the caller.
 */
static int _mqtt3_db_regex_create(const char *topic, char **regex)
{
	char *stmp;
	int hier;
	static char *local_regex = NULL;
	static int regex_len = 0;
	char *local_topic;
	int new_len;
	char *token;
	int pos;
	int i;
	int sys = 0;
	char *start_slash;

	if(!topic || !regex) return 1;

	if(!strncmp(topic, "$SYS", 4)){
		sys = 1;
	}
	hier = 0;
	if(topic[0] == '/'){
		local_topic = _mosquitto_strdup(topic+1);
		start_slash = "/";
	}else{
		local_topic = _mosquitto_strdup(topic);
		start_slash = "";
	}
	if(!local_topic) return 1;
	stmp = local_topic;
	while(stmp){
		stmp = index(stmp, '/');
		if(stmp) stmp++;
		hier++;
	}
	if(hier > 1){
		new_len = strlen(local_topic) - (hier-1)
			  + 24 /* For hier==1 start */
			  + 24*(hier-2) /* For hier>1 && hier<(max-1) start */
			  + 15 /* For final hier start */
			  + 5*(hier-2) /* For hier>1 end */
			  + 6 /* For hier==1 and NULL */
			  + 4*hier; /* For \Q ... \E per hierarchy level */
	}else{
		new_len = strlen(local_topic) + 21;
	}
	if(regex_len < new_len){
		local_regex = _mosquitto_realloc(local_regex, new_len);
		regex_len = new_len;
		if(!local_regex) return 1;
	}
	if(hier > 1){
		token = strtok(local_topic, "/");
		if(!sys){
			pos = sprintf(local_regex, "^%s(?:(?:(?:\\Q%s\\E|\\+)(?!$))", start_slash, token);
		}else{
			pos = sprintf(local_regex, "^(?:(?:(?:\\Q%s\\E)(?!$))", token);
		}
		token = strtok(NULL, "/");
		i=1;
		while(token){
			if(i < hier-1){
				pos += sprintf(&(local_regex[pos]), "(?:(?:/(?:(?:\\Q%s\\E|\\+)(?!$)))", token);
			}else{
				pos += sprintf(&(local_regex[pos]), "(?:(?:/(?:\\Q%s\\E|\\+))", token);
			}
			token = strtok(NULL, "/");
			i++;
		}
		for(i=0; i<hier-1; i++){
			pos += sprintf(&(local_regex[pos]), "|/#)?");
		}
		if(!sys){
			sprintf(&(local_regex[pos]), "|#)$");
		}else{
			sprintf(&(local_regex[pos]), ")$");
		}
	}else{
		if(!sys){
			pos = sprintf(local_regex, "^%s(?:(?:(?:\\Q%s\\E|\\+))|#)$", start_slash, local_topic);
		}else{
			pos = sprintf(local_regex, "^(?:(?:(?:%s|\\+)))$", local_topic);
		}
	}
	*regex = local_regex;
	_mosquitto_free(local_topic);
	return 0;
}

static int _mqtt3_db_retain_regex_create(const char *sub, char **regex)
{
	char *stmp;
	int hier;
	char *local_regex = NULL;
	int regex_len;
	char *local_sub;
	char *token;
	int pos;
	int i;
	char *sys;

	if(!sub || !regex) return 1;

	if(strncmp(sub, "$SYS", 4)){
		if(sub[0] == '/'){
			sys = "^/(?!\\$SYS)";
			local_sub = _mosquitto_strdup(sub+1);
		}else{
			sys = "^(?!\\$SYS)";
			local_sub = _mosquitto_strdup(sub);
		}
	}else{
		sys = "^";
		local_sub = _mosquitto_strdup(sub);
	}
	if(!local_sub) return 1;
	hier = 0;
	stmp = local_sub;
	while(stmp){
		stmp = index(stmp, '/');
		if(stmp) stmp++;
		hier++;
	}
	regex_len = strlen(sys) + strlen(local_sub) + 5*hier + 2;
	local_regex = _mosquitto_realloc(local_regex, regex_len);
	if(!local_regex) return 1;

	if(hier > 1){
		token = strtok(local_sub, "/");
		if(!strcmp(token, "+")){
			pos = sprintf(local_regex, "%s[^/]+", sys);
		}else{
			pos = sprintf(local_regex, "%s\\Q%s\\E", sys, token);
		}
		for(i=1; i<hier-1; i++){
			token = strtok(NULL, "/");
			if(token){
				/* Token may be NULL here if there are multiple / in a row in
				 * the subscription string. */
				if(!strcmp(token, "+")){
					pos += sprintf(&(local_regex[pos]), "/[^/]+");
				}else{
					pos += sprintf(&(local_regex[pos]), "/\\Q%s\\E", token);
				}
			}
		}
		token = strtok(NULL, "/");
		if(token){
			if(!strcmp(token, "+")){
				pos += sprintf(&(local_regex[pos]), "/[^/]+$");
			}else if(!strcmp(token, "#")){
				pos += sprintf(&(local_regex[pos]), "/.*");
			}else{
				pos += sprintf(&(local_regex[pos]), "/\\Q%s\\E$", token);
			}
		}else{
			/* If token is NULL, this means we have one hierarchy too few.
			 * This is caused by the first character of the sub being / or by
			 * multiple / (ie //) appearing in a row. */
			pos += sprintf(&(local_regex[pos]), "$");
		}
	}else{
		token = strtok(local_sub, "/");
		if(!strcmp(token, "+")){
			pos = sprintf(local_regex, "%s[^/]+$", sys);
		}else if(!strcmp(token, "#")){
			pos = sprintf(local_regex, "%s.*", sys);
		}else{
			pos = sprintf(local_regex, "%s\\Q%s\\E$", sys, token);
		}
	}
	*regex = local_regex;
	_mosquitto_free(local_sub);
	return 0;
}
#endif

/* Add a retained message to the database for a particular topic.
 * Only one retained message exists per topic, so does an update if one already exists.
 * Returns 1 on failure (topic is NULL, sqlite error)
 * Returns 0 on success.
 */
int mqtt3_db_retain_insert(const char *topic, int64_t store_id)
{
	/* Warning: Don't start transaction in this function. */
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!topic || !store_id) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("REPLACE INTO retain (topic,store_id) VALUES (?,?)");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, topic, strlen(topic), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int64(stmt, 2, store_id) != SQLITE_OK) rc = 1;
	if(!rc){
		if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	}
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

int mqtt3_db_retain_delete(const char *topic)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!topic) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("DELETE FROM retain WHERE topic=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, topic, strlen(topic), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(!rc){
		if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	}
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

int mqtt3_db_retain_queue(mqtt3_context *context, const char *sub, int sub_qos)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;
#ifdef WITH_REGEX
	char *regex = NULL;
#endif
	const char *topic;
	int qos;
	int64_t store_id;
	uint16_t mid;

	if(!stmt){
#ifdef WITH_REGEX
		stmt = _mqtt3_db_statement_prepare("SELECT message_store.topic,qos,id FROM message_store "
				"JOIN retain ON message_store.id=retain.store_id WHERE regexp(?, retain.topic)");
#else
		stmt = _mqtt3_db_statement_prepare("SELECT message_store.topic,qos,id FROM message_store "
				"JOIN retain ON message_store.id=retain.store_id WHERE retain.topic=?");
#endif
		if(!stmt) return 1;
	}
#ifdef WITH_REGEX
	if(_mqtt3_db_retain_regex_create(sub, &regex)) return 1;
#endif

#ifdef WITH_REGEX
	if(sqlite3_bind_text(stmt, 1, regex, strlen(regex), _mosquitto_free) != SQLITE_OK) rc = 1;
#else
	if(sqlite3_bind_text(stmt, 1, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;
#endif

	while(sqlite3_step(stmt) == SQLITE_ROW){
		topic = (const char *)sqlite3_column_text(stmt, 0);
		qos = sqlite3_column_int(stmt, 1);
		store_id = sqlite3_column_int64(stmt, 2);

		if(qos > sub_qos) qos = sub_qos;
		if(qos > 0){
			mid = mqtt3_db_mid_generate(context->id);
		}else{
			mid = 0;
		}
		switch(qos){
			case 0:
				if(mqtt3_db_message_insert(context->id, mid, mosq_md_out, ms_publish, qos, store_id) == 1) rc = 1;
				break;
			case 1:
				if(mqtt3_db_message_insert(context->id, mid, mosq_md_out, ms_publish_puback, qos, store_id) == 1) rc = 1;
				break;
			case 2:
				if(mqtt3_db_message_insert(context->id, mid, mosq_md_out, ms_publish_pubrec, qos, store_id) == 1) rc = 1;
				break;
		}
	}
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	return rc;
}

int mqtt3_db_store_clean(void)
{
	static sqlite3_stmt *stmt = NULL;
	int rc = 0;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("DELETE FROM message_store "
				"WHERE id NOT IN (SELECT store_id FROM messages) "
				"AND id NOT IN (SELECT store_id FROM retain)"); 
		if(!stmt){
			return 1;
		}
	}

	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);

	return rc;
}

/* Save a subscription for a client.
 * Returns 1 on failure (client_id or sub is NULL, sqlite error)
 * Returns 0 on success.
 * If a subscription already exists for this client, it will be updated.
 */
int mqtt3_db_sub_insert(const char *client_id, const char *sub, int qos)
{
	int rc = 0;
	static sqlite3_stmt *stmt_insert = NULL;
	static sqlite3_stmt *stmt_select = NULL;
	static sqlite3_stmt *stmt_update = NULL;

	if(!client_id || !sub) return 1;

	if(!stmt_insert){
		stmt_insert = _mqtt3_db_statement_prepare("INSERT INTO subs (client_id,sub,qos) VALUES (?,?,?)");
		if(!stmt_insert){
			return 1;
		}
	}
	if(!stmt_select){
		stmt_select = _mqtt3_db_statement_prepare("SELECT 1 FROM subs WHERE client_id=? AND sub=?");
		if(!stmt_select){
			return 1;
		}
	}
	if(!stmt_update){
		stmt_update = _mqtt3_db_statement_prepare("UPDATE subs SET qos=? WHERE client_id=? AND sub=?");
		if(!stmt_update){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt_select, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_text(stmt_select, 2, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt_select) == SQLITE_ROW){
		/* Entry already exists, so update */
		if(sqlite3_bind_int(stmt_update, 1, qos) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_text(stmt_update, 2, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_text(stmt_update, 3, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_step(stmt_update) != SQLITE_DONE) rc = 1;
		sqlite3_reset(stmt_update);
		sqlite3_clear_bindings(stmt_update);
	}else{
		/* Insert */
		if(sqlite3_bind_text(stmt_insert, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_text(stmt_insert, 2, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(stmt_insert, 3, qos) != SQLITE_OK) rc = 1;
		if(sqlite3_step(stmt_insert) != SQLITE_DONE) rc = 1;
		sqlite3_reset(stmt_insert);
		sqlite3_clear_bindings(stmt_insert);
	}
	sqlite3_reset(stmt_select);
	sqlite3_clear_bindings(stmt_select);

	return rc;
}

/* Remove a subscription for a client.
 * Returns 1 on failure (client_id or sub are NULL, sqlite error)
 * Returns 0 on success.
 * Will return success if no subscription exists for the given client_id/sub.
 */
int mqtt3_db_sub_delete(const char *client_id, const char *sub)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!client_id || !sub) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("DELETE FROM subs WHERE client_id=? AND sub=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_text(stmt, 2, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

/* Begin a new search for subscriptions that match 'topic'.
 * Returns 1 on failure (topic is NULL, sqlite error)
 * Returns 0 on failure.
 * Will use regex for pattern matching if compiled with WITH_REGEX defined.
 * Wildcards in subscriptions are disabled if WITH_REGEX not defined.
 */
int mqtt3_db_sub_search_start(const char *source_id, const char *topic, int qos)
{
	/* Warning: Don't start transaction in this function. */
	int rc = 0;
#ifdef WITH_REGEX
	char *regex;
#endif

	if(!source_id || !topic) return 1;

	if(!stmt_sub_search){
		/* Only queue messages for clients that are connected, or clients that
		 * are disconnected and have QoS>0. */
#ifdef WITH_REGEX
		stmt_sub_search = _mqtt3_db_statement_prepare("SELECT client_id,qos FROM subs "
				"JOIN clients ON subs.client_id=clients.id "
				"WHERE regexp(?, subs.sub)"
				" AND ((clients.sock=-1 AND subs.qos<>0 AND ?<>0) OR clients.sock<>-1)"
				" AND (clients.is_bridge=0 OR (clients.is_bridge=1 AND clients.id<>?))");
#else
		stmt_sub_search = _mqtt3_db_statement_prepare("SELECT client_id,qos FROM subs "
				"JOIN clients ON subs.client_id=clients.id "
				"WHERE subs.sub=?"
				" AND ((clients.sock=-1 AND subs.qos<>0 AND ?<>0) OR clients.sock<>-1)"
				" AND (clients.is_bridge=0 OR (clients.is_bridge=1 AND clients.id<>?))");
#endif
		if(!stmt_sub_search){
			return 1;
		}
	}

#ifdef WITH_REGEX
	if(_mqtt3_db_regex_create(topic, &regex)) return 1;
	if(sqlite3_bind_text(stmt_sub_search, 1, regex, strlen(regex), SQLITE_STATIC) != SQLITE_OK) rc = 1;
#else
	if(sqlite3_bind_text(stmt_sub_search, 1, topic, strlen(topic), SQLITE_STATIC) != SQLITE_OK) rc = 1;
#endif
	if(sqlite3_bind_int(stmt_sub_search, 2, qos) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_text(stmt_sub_search, 3, source_id, strlen(source_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;

	return rc;
}

/* Remove all subscriptions for a client.
 * Returns 1 on failure (client_id is NULL, sqlite error)
 * Returns 0 on success.
 */
int mqtt3_db_subs_clean_session(const char *client_id)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!client_id) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("DELETE FROM subs WHERE client_id=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

/* Send messages for the $SYS hierarchy if the last update is longer than
 * 'interval' seconds ago.
 * 'interval' is the amount of seconds between updates. If 0, then no periodic
 * messages are sent for the $SYS hierarchy.
 * 'start_time' is the result of time() that the broker was started at.
 */
void mqtt3_db_sys_update(int interval, time_t start_time)
{
	static time_t last_update = 0;
	time_t now = time(NULL);
	char buf[100];
	int count;

	if(interval && now - interval > last_update){
		_mqtt3_db_transaction_begin();

		snprintf(buf, 100, "%d seconds", (int)(now - start_time));
		mqtt3_db_messages_easy_queue("", "$SYS/broker/uptime", 2, strlen(buf), (uint8_t *)buf, 1);

		if(!mqtt3_db_message_count(&count)){
			snprintf(buf, 100, "%d", count);
			mqtt3_db_messages_easy_queue("", "$SYS/broker/messages/inflight", 2, strlen(buf), (uint8_t *)buf, 1);
		}

		if(!mqtt3_db_client_count(&count)){
			snprintf(buf, 100, "%d", count);
			mqtt3_db_messages_easy_queue("", "$SYS/broker/clients/total", 2, strlen(buf), (uint8_t *)buf, 1);
		}

#ifdef WITH_MEMORY_TRACKING
		snprintf(buf, 100, "%lld", _mosquitto_memory_used()+sqlite3_memory_used());
		mqtt3_db_messages_easy_queue("", "$SYS/broker/heap/current size", 2, strlen(buf), (uint8_t *)buf, 1);
#endif

		snprintf(buf, 100, "%lu", mqtt3_net_msgs_total_received());
		mqtt3_db_messages_easy_queue("", "$SYS/broker/messages/received", 2, strlen(buf), (uint8_t *)buf, 1);
		
		snprintf(buf, 100, "%lu", mqtt3_net_msgs_total_sent());
		mqtt3_db_messages_easy_queue("", "$SYS/broker/messages/sent", 2, strlen(buf), (uint8_t *)buf, 1);

		snprintf(buf, 100, "%llu", (unsigned long long)mqtt3_net_bytes_total_received());
		mqtt3_db_messages_easy_queue("", "$SYS/broker/bytes/received", 2, strlen(buf), (uint8_t *)buf, 1);
		
		snprintf(buf, 100, "%llu", (unsigned long long)mqtt3_net_bytes_total_sent());
		mqtt3_db_messages_easy_queue("", "$SYS/broker/bytes/sent", 2, strlen(buf), (uint8_t *)buf, 1);
		
		last_update = time(NULL);

		_mqtt3_db_transaction_end();
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

/* Internal function.
 * Begin a sqlite transaction.
 * Returns 1 on failure (sqlite error)
 * Returns 0 on success.
 */
static int _mqtt3_db_transaction_begin(void)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("BEGIN TRANSACTION");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);

	return rc;
}

/* Internal function.
 * End a sqlite transaction.
 * Returns 1 on failure (sqlite error)
 * Returns 0 on success.
 */
static int _mqtt3_db_transaction_end(void)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("END TRANSACTION");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);

	return rc;
}

#if 0
/* Internal function.
 * Roll back a sqlite transaction.
 * Returns 1 on failure (sqlite error)
 * Returns 0 on success.
 */
static int _mqtt3_db_transaction_rollback(void)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("ROLLBACK TRANSACTION");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);

	return rc;
}
#endif

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

