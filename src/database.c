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
#include <unistd.h>

#include <config.h>
#include <mqtt3.h>

static sqlite3 *db = NULL;
static char *db_filepath = NULL;

/* Need to make a struct here to have an array.
 * Don't know size of sqlite3_stmt so can't array it directly.
 */
struct stmt_array {
	sqlite3_stmt *stmt;
};
static int g_stmt_count = 0;
static struct stmt_array *g_stmts = NULL;
static sqlite3_stmt *stmt_sub_search = NULL;

static int _mqtt3_db_tables_create(void);
static int _mqtt3_db_invalidate_sockets(void);
#ifdef WITH_REGEX
static int _mqtt3_db_regex_create(const char *topic, char **regex);
static int _mqtt3_db_retain_regex_create(const char *sub, char **regex);
#endif
static sqlite3_stmt *_mqtt3_db_statement_prepare(const char *query);
static void _mqtt3_db_statements_finalize(void);
static int _mqtt3_db_version_check(void);
static int _mqtt3_db_transaction_begin(void);
static int _mqtt3_db_transaction_end(void);
#if 0
static int _mqtt3_db_transaction_rollback(void);
#endif

#ifdef WITH_CLIENT
/* Client callback for publish events - this WILL change. */
int (*client_publish_callback)(const char *, int, uint32_t, const uint8_t *, int) = NULL;
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
			db_filepath = mqtt3_malloc(strlen(config->persistence_location) + strlen("mosquitto.db") + 1);
			if(!db_filepath) return 1;
			sprintf(db_filepath, "%smosquitto.db", config->persistence_location);
		}else{
			db_filepath = mqtt3_strdup("mosquitto.db");
		}
		dbrc = sqlite3_open_v2(db_filepath, &restore_db, SQLITE_OPEN_READONLY, NULL);
		/* FIXME - need to handle all error conditions, *especially* file doesn't exist. */
		if(dbrc == SQLITE_OK){
			restore = sqlite3_backup_init(db, "main", restore_db, "main");
			if(restore){
				sqlite3_backup_step(restore, -1);
				sqlite3_backup_finish(restore);
				if(_mqtt3_db_version_check()){
					mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Invalid database version.");
					rc = 1;
				}
			}else{
				mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Couldn't restore database %s (%d).", db_filepath, dbrc);
				rc = 1;
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
	if(_mqtt3_db_invalidate_sockets()) return 1;

	return rc;
}

int mqtt3_db_close(void)
{
	_mqtt3_db_statements_finalize();

	sqlite3_close(db);
	db = NULL;

	sqlite3_shutdown();

	return 0;
}

int mqtt3_db_backup(bool vacuum_backup)
{
	char *errmsg = NULL;
	int rc = 0;
	sqlite3 *backup_db;
	sqlite3_backup *backup;

	if(!db || !db_filepath) return 1;
	mqtt3_log_printf(MQTT3_LOG_INFO, "Saving in-memory database to %s.", db_filepath);
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
	if(vacuum_backup){
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
		"clean_start INTEGER, "
		"will INTEGER, will_retain INTEGER, will_qos INTEGER, "
		"will_topic TEXT, will_message TEXT, "
		"last_mid INTEGER)",
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
		"topic TEXT, qos INTEGER, payloadlen INTEGER, payload BLOB)",
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
		"retain INTEGER, topic TEXT, payloadlen INTEGER, payload BLOB)",
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
		"mid INTEGER, dup INTEGER, qos INTEGER, retain INTEGER, topic TEXT, payloadlen INTEGER, payload BLOB)",
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

/* Internal function.
 * Finalise all sqlite statements held in g_stmts. This must be done before
 * closing the db.
 * See also _mqtt3_db_statement_prepare().
 */
static void _mqtt3_db_statements_finalize(void)
{
	int i;
	for(i=0; i<g_stmt_count; i++){
		if(g_stmts[i].stmt){
			sqlite3_finalize(g_stmts[i].stmt);
		}
	}
	mqtt3_free(g_stmts);
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
			close(oldsock);
		}
		mqtt3_db_client_update(context, will, will_retain, will_qos, will_topic, will_message);
	}else{
		if(!stmt){
			stmt = _mqtt3_db_statement_prepare("INSERT INTO clients "
					"(sock,id,clean_start,will,will_retain,will_qos,will_topic,will_message,last_mid) "
					"SELECT ?,?,?,?,?,?,?,?,0 WHERE NOT EXISTS "
					"(SELECT 1 FROM clients WHERE id=?)");
			if(!stmt){
				return 1;
			}
		}
		if(sqlite3_bind_int(stmt, 1, context->sock) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_text(stmt, 2, context->id, strlen(context->id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(stmt, 3, context->clean_start) != SQLITE_OK) rc = 1;
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
		if(sqlite3_bind_text(stmt, 9, context->id, strlen(context->id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
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
				"sock=?,clean_start=?,will=?,will_retain=?,will_qos=?,"
				"will_topic=?,will_message=? WHERE id=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_int(stmt, 1, context->sock) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 2, context->clean_start) != SQLITE_OK) rc = 1;
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

	if(!context || !context->id) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("SELECT will_topic,will_qos,will_message,will_retain FROM clients WHERE id=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, context->id, strlen(context->id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	dbrc = sqlite3_step(stmt);
	if(dbrc == SQLITE_ROW){
		topic = (const char *)sqlite3_column_text(stmt, 0);
		qos = sqlite3_column_int(stmt, 1);
		payload = sqlite3_column_text(stmt, 2);
		retain = sqlite3_column_int(stmt, 3);
		if(!rc){
			if(mqtt3_db_messages_queue(topic, qos, strlen((const char *)payload), payload, retain)) rc = 1;
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
 * Set all stored sockets to -1 (invalid) when closing mosquitto.
 * Returns 1 on failure (sqlite error)
 * Returns 0 on success.
 */
static int _mqtt3_db_invalidate_sockets(void)
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

int mqtt3_db_message_delete(const char *client_id, uint16_t mid, mqtt3_msg_direction dir)
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

int mqtt3_db_message_delete_by_oid(uint64_t oid)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("DELETE FROM messages WHERE OID=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_int(stmt, 1, oid) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

int mqtt3_db_message_insert(const char *client_id, uint16_t mid, mqtt3_msg_direction dir, mqtt3_msg_status status, int retain, const char *topic, int qos, uint32_t payloadlen, const uint8_t *payload)
{
	/* Warning: Don't start transaction in this function. */
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!client_id || !topic || !payload) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("INSERT INTO messages "
				"(client_id, timestamp, direction, status, mid, dup, qos, retain, topic, payloadlen, payload) "
				"VALUES (?,?,?,?,?,0,?,?,?,?,?)");
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
	if(sqlite3_bind_int(stmt, 7, retain) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_text(stmt, 8, topic, strlen(topic), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 9, payloadlen) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_blob(stmt, 10, payload, payloadlen, SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

int mqtt3_db_message_update(const char *client_id, uint16_t mid, mqtt3_msg_direction dir, mqtt3_msg_status status)
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

int mqtt3_db_messages_easy_queue(const char *topic, int qos, uint32_t payloadlen, const uint8_t *payload, int retain)
{
	int64_t store_id;

	if(!topic || !payloadlen || !payload) return 1;

	if(mqtt3_db_message_store(topic, qos, payloadlen, payload, retain, &store_id)) return 1;

	return mqtt3_db_messages_queue(topic, qos, payloadlen, payload, retain);
}

int mqtt3_db_messages_queue(const char *topic, int qos, uint32_t payloadlen, const uint8_t *payload, int retain)
{
	/* Warning: Don't start transaction in this function. */
	int rc = 0;
	char *client_id;
	uint8_t client_qos;
	uint8_t msg_qos;
	uint16_t mid;

	/* Find all clients that subscribe to topic and put messages into the db for them. */
	if(!topic || !payloadlen || !payload) return 1;

#ifdef WITH_CLIENT
	if(client_publish_callback){
		client_publish_callback(topic, qos, payloadlen, payload, retain);
	}
#endif
	if(retain){
		if(mqtt3_db_retain_insert(topic, qos, payloadlen, payload)) rc = 1;
	}
	if(!mqtt3_db_sub_search_start(topic)){
		while(!mqtt3_db_sub_search_next(&client_id, &client_qos)){
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
					if(mqtt3_db_message_insert(client_id, mid, md_out, ms_publish, retain, topic, msg_qos, payloadlen, payload)) rc = 1;
					break;
				case 1:
					if(mqtt3_db_message_insert(client_id, mid, md_out, ms_publish_puback, retain, topic, msg_qos, payloadlen, payload)) rc = 1;
					break;
				case 2:
					if(mqtt3_db_message_insert(client_id, mid, md_out, ms_publish_pubrec, retain, topic, msg_qos, payloadlen, payload)) rc = 1;
					break;
			}
			if(client_id) mqtt3_free(client_id);
		}
	}
	return rc;
}

int mqtt3_db_message_store(const char *topic, int qos, uint32_t payloadlen, const uint8_t *payload, int retain, int64_t *store_id)
{
	/* Warning: Don't start transaction in this function. */
	static sqlite3_stmt *stmt = NULL;
	int rc = 0;

	if(!topic || !payloadlen || !payload || !store_id) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("INSERT INTO message_store "
				"(timestamp, qos, retain, topic, payloadlen, payload) "
				"VALUES (?,?,?,?,?,?)");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_int(stmt, 1, time(NULL)) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 2, qos) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 3, retain) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_text(stmt, 4, topic, strlen(topic), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 5, payloadlen) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_blob(stmt, 6, payload, payloadlen, SQLITE_STATIC) != SQLITE_OK) rc = 1;
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
	uint64_t OID;
	int status;
	mqtt3_msg_status new_status = ms_invalid;

	if(!stmt_select){
		stmt_select = _mqtt3_db_statement_prepare("SELECT OID,status FROM messages WHERE timestamp<?");
		if(!stmt_select){
			return 1;
		}
	}
	if(!stmt_update){
		stmt_update = _mqtt3_db_statement_prepare("UPDATE messages SET status=?,dup=1 WHERE OID=?");
		if(!stmt_update){
			return 1;
		}
	}
	_mqtt3_db_transaction_begin();
	if(sqlite3_bind_int(stmt_select, 1, now) != SQLITE_OK) rc = 1;
	while(sqlite3_step(stmt_select) == SQLITE_ROW){
		OID = sqlite3_column_int(stmt_select, 0);
		status = sqlite3_column_int(stmt_select, 1);
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
			if(sqlite3_bind_int(stmt_update, 2, OID) != SQLITE_OK) rc = 1;
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

int mqtt3_db_message_release(const char *client_id, uint16_t mid, mqtt3_msg_direction dir)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;
	uint64_t OID;
	const char *topic;
	int qos;
	int payloadlen;
	const uint8_t *payload;
	int retain;

	if(!client_id) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("SELECT OID,topic,qos,payloadlen,payload,retain FROM messages WHERE client_id=? AND mid=? AND direction=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 2, mid) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 3, dir) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) == SQLITE_ROW){
		OID = sqlite3_column_int(stmt, 0);
		topic = (const char *)sqlite3_column_text(stmt, 1);
		qos = sqlite3_column_int(stmt, 2);
		payloadlen = sqlite3_column_int(stmt, 3);
		payload = sqlite3_column_blob(stmt, 4);
		retain = sqlite3_column_int(stmt, 5);
		if(!mqtt3_db_messages_queue(topic, qos, payloadlen, payload, retain)){
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
	uint64_t OID;
	int status;
	uint16_t mid;
	int dup;
	int retain;
	const char *topic;
	int qos;
	uint32_t payloadlen;
	const uint8_t *payload;

	if(!context || !context->id || context->sock == -1) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("SELECT OID,status,mid,dup,retain,topic,qos,payloadlen,payload FROM messages "
				"WHERE (status=1 OR status=2 OR status=4 OR status=6 OR status=8) "
				"AND direction=1 AND client_id=? ORDER BY timestamp");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, context->id, strlen(context->id), SQLITE_STATIC) == SQLITE_OK){
		_mqtt3_db_transaction_begin();
		while(sqlite3_step(stmt) == SQLITE_ROW){
			OID = sqlite3_column_int(stmt, 0);
			status = sqlite3_column_int(stmt, 1);
			mid = sqlite3_column_int(stmt, 2);
			dup = sqlite3_column_int(stmt, 3);
			retain = sqlite3_column_int(stmt, 4);
			topic = (const char *)sqlite3_column_text(stmt, 5);
			qos = sqlite3_column_int(stmt, 6);
			payloadlen = sqlite3_column_int(stmt, 7);
			payload = sqlite3_column_blob(stmt, 8);
			switch(status){
				case ms_publish:
					if(!mqtt3_raw_publish(context, dup, qos, retain, mid, topic, payloadlen, payload)){
						mqtt3_db_message_delete_by_oid(OID);
					}
					break;

				case ms_publish_puback:
					if(!mqtt3_raw_publish(context, dup, qos, retain, mid, topic, payloadlen, payload)){
						mqtt3_db_message_update(context->id, mid, md_out, ms_wait_puback);
					}
					break;

				case ms_publish_pubrec:
					if(!mqtt3_raw_publish(context, dup, qos, retain, mid, topic, payloadlen, payload)){
						mqtt3_db_message_update(context->id, mid, md_out, ms_wait_pubrec);
					}
					break;
				
				case ms_resend_pubrel:
					if(!mqtt3_raw_pubrel(context, mid)){
						mqtt3_db_message_update(context->id, mid, md_out, ms_wait_pubrel);
					}
					break;

				case ms_resend_pubcomp:
					if(!mqtt3_raw_pubcomp(context, mid)){
						mqtt3_db_message_update(context->id, mid, md_out, ms_wait_pubcomp);
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

	if(!topic || !regex) return 1;

	local_topic = mqtt3_strdup(topic);
	if(!local_topic) return 1;

	if(!strncmp(local_topic, "$SYS", 4)){
		sys = 1;
	}
	hier = 0;
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
		local_regex = mqtt3_realloc(local_regex, new_len);
		regex_len = new_len;
		if(!local_regex) return 1;
	}
	if(hier > 1){
		token = strtok(local_topic, "/");
		if(!sys){
			pos = sprintf(local_regex, "^(?:(?:(?:\\Q%s\\E|\\+)(?!$))", token);
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
			pos = sprintf(local_regex, "^(?:(?:(?:\\Q%s\\E|\\+))|#)$", local_topic);
		}else{
			pos = sprintf(local_regex, "^(?:(?:(?:%s|\\+)))$", local_topic);
		}
	}
	*regex = local_regex;
	mqtt3_free(local_topic);
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
		sys = "^(?!\\$SYS)";
	}else{
		sys = "^";
	}
	local_sub = mqtt3_strdup(sub);
	if(!local_sub) return 1;

	hier = 0;
	stmp = local_sub;
	while(stmp){
		stmp = index(stmp, '/');
		if(stmp) stmp++;
		hier++;
	}
	regex_len = strlen(sys) + strlen(local_sub) + 5*hier + 2;
	local_regex = mqtt3_realloc(local_regex, regex_len);
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
	mqtt3_free(local_sub);
	return 0;
}
#endif

/* Find a single retained message matching 'topic'.
 * Return qos, payloadlen and payload only if those arguments are not NULL.
 * Returns 1 on failure (topic is NULL, sqlite error) or no retained message found.
 * Returns 0 on retained message successfully found.
 */
int mqtt3_db_retain_find(const char *topic, int *qos, uint32_t *payloadlen, uint8_t **payload)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;
	const uint8_t *payloadtmp;

	if(!topic) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("SELECT qos,payloadlen,payload FROM retain WHERE topic=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, topic, strlen(topic), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) == SQLITE_ROW){
		if(qos) *qos = sqlite3_column_int(stmt, 0);
		if(payloadlen) *payloadlen = sqlite3_column_int(stmt, 1);
		if(payload && payloadlen && *payloadlen){
			*payload = mqtt3_malloc(*payloadlen);
			payloadtmp = sqlite3_column_blob(stmt, 2);
			memcpy(*payload, payloadtmp, *payloadlen);
		}
	}else{
		rc = 1;
	}
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

/* Add a retained message to the database for a particular topic.
 * Only one retained message exists per topic, so does an update if one already exists.
 * Returns 1 on failure (topic, or payload are NULL, payloadlen is 0, sqlite error)
 * Returns 0 on success.
 */
int mqtt3_db_retain_insert(const char *topic, int qos, uint32_t payloadlen, const uint8_t *payload)
{
	/* Warning: Don't start transaction in this function. */
	int rc = 0;
	static sqlite3_stmt *stmt_update = NULL;
	static sqlite3_stmt *stmt_insert = NULL;

	if(!topic || !payloadlen || !payload) return 1;

	if(!mqtt3_db_retain_find(topic, NULL, NULL, NULL)){
		if(!stmt_update){
			stmt_update = _mqtt3_db_statement_prepare("UPDATE retain SET qos=?,payloadlen=?,payload=? WHERE topic=?");
			if(!stmt_update){
				return 1;
			}
		}
		if(sqlite3_bind_int(stmt_update, 1, qos) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(stmt_update, 2, payloadlen) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_blob(stmt_update, 3, payload, payloadlen, SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_text(stmt_update, 4, topic, strlen(topic), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_step(stmt_update) != SQLITE_DONE) rc = 1;
		sqlite3_reset(stmt_update);
		sqlite3_clear_bindings(stmt_update);
	}else{
		if(!stmt_insert){
			stmt_insert = _mqtt3_db_statement_prepare("INSERT INTO retain VALUES (?,?,?,?)");
			if(!stmt_insert){
				return 1;
			}
		}
		if(sqlite3_bind_text(stmt_insert, 1, topic, strlen(topic), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(stmt_insert, 2, qos) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(stmt_insert, 3, payloadlen) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_blob(stmt_insert, 4, payload, payloadlen, SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_step(stmt_insert) != SQLITE_DONE) rc = 1;
		sqlite3_reset(stmt_insert);
		sqlite3_clear_bindings(stmt_insert);
	}

	return rc;
}

int mqtt3_db_retain_queue(mqtt3_context *context, const char *sub, int sub_qos)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;;
#ifdef WITH_REGEX
	char *regex = NULL;
#endif
	const char *topic;
	int qos;
	const uint8_t *payload;
	uint32_t payloadlen;
	uint16_t mid;

	if(!stmt){
#ifdef WITH_REGEX
		stmt = _mqtt3_db_statement_prepare("SELECT topic,qos,payloadlen,payload FROM retain WHERE regexp(?, topic)");
#else
		stmt = _mqtt3_db_statement_prepare("SELECT topic,qos,payloadlen,payload FROM retain WHERE topic=?");
#endif
		if(!stmt) return 1;
	}
#ifdef WITH_REGEX
	if(_mqtt3_db_retain_regex_create(sub, &regex)) return 1;
#endif

#ifdef WITH_REGEX
	if(sqlite3_bind_text(stmt, 1, regex, strlen(regex), mqtt3_free) != SQLITE_OK) rc = 1;
#else
	if(sqlite3_bind_text(stmt, 1, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;
#endif

	while(sqlite3_step(stmt) == SQLITE_ROW){
		topic = (const char *)sqlite3_column_text(stmt, 0);
		qos = sqlite3_column_int(stmt, 1);
		payloadlen = sqlite3_column_int(stmt, 2);
		payload = sqlite3_column_blob(stmt, 3);

		if(qos > sub_qos) qos = sub_qos;
		if(qos > 0){
			mid = mqtt3_db_mid_generate(context->id);
		}else{
			mid = 0;
		}
		switch(qos){
			case 0:
				if(mqtt3_db_message_insert(context->id, mid, md_out, ms_publish, 1,
						topic, qos, payloadlen, payload)) rc = 1;
				break;
			case 1:
				if(mqtt3_db_message_insert(context->id, mid, md_out, ms_publish_puback, 1,
						topic, qos, payloadlen, payload)) rc = 1;
				break;
			case 2:
				if(mqtt3_db_message_insert(context->id, mid, md_out, ms_publish_pubrec, 1,
						topic, qos, payloadlen, payload)) rc = 1;
				break;
		}
	}
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	return rc;
}

int mqtt3_db_store_clean(void)
{
	static sqlite3_stmt *stmt = NULL;;
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
int mqtt3_db_sub_search_start(const char *topic)
{
	/* Warning: Don't start transaction in this function. */
	int rc = 0;
#ifdef WITH_REGEX
	char *regex;
#endif

	if(!topic) return 1;

	if(!stmt_sub_search){
#ifdef WITH_REGEX
		stmt_sub_search = _mqtt3_db_statement_prepare("SELECT client_id,qos FROM subs "
				"JOIN clients ON subs.client_id=clients.id "
				"WHERE (subs.qos<>0 OR clients.sock<>-1) AND regexp(?, subs.sub)");
#else
		stmt_sub_search = _mqtt3_db_statement_prepare("SELECT client_id,qos FROM subs "
				"JOIN clients ON subs.client_id=clients.id "
				"WHERE subs.sub=? AND (subs.qos<>0 OR clients.sock<>-1)");
#endif
		if(!stmt_sub_search){
			return 1;
		}
	}
	sqlite3_reset(stmt_sub_search);
	sqlite3_clear_bindings(stmt_sub_search);

#ifdef WITH_REGEX
	if(_mqtt3_db_regex_create(topic, &regex)) return 1;
	if(sqlite3_bind_text(stmt_sub_search, 1, regex, strlen(regex), SQLITE_STATIC) != SQLITE_OK) rc = 1;
#else
	if(sqlite3_bind_text(stmt_sub_search, 1, topic, strlen(topic), SQLITE_STATIC) != SQLITE_OK) rc = 1;
#endif

	return rc;
}

/* Retrieve next matching subscription from search.
 * mqtt3_db_sub_search_start() must have been called prior to calling this function.
 * Returns matching client id and qos in client_id and qos, which must not be NULL.
 * Returns 1 on failure (m_d_s_s_s() not called, client_id or qos is NULL, sqlite error)
 * Returns 0 on success.
 */
int mqtt3_db_sub_search_next(char **client_id, uint8_t *qos)
{
	/* Warning: Don't start transaction in this function. */
	if(!stmt_sub_search || !client_id || !qos) return 1;
	if(sqlite3_step(stmt_sub_search) != SQLITE_ROW){
		return 1;
	}
	*client_id = mqtt3_strdup((char *)sqlite3_column_text(stmt_sub_search, 0));
	*qos = sqlite3_column_int(stmt_sub_search, 1);

	return 0;
}

/* Remove all subscriptions for a client.
 * Returns 1 on failure (client_id is NULL, sqlite error)
 * Returns 0 on success.
 */
int mqtt3_db_subs_clean_start(const char *client_id)
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
		mqtt3_db_messages_queue("$SYS/broker/uptime", 2, strlen(buf), (uint8_t *)buf, 1);

		if(!mqtt3_db_message_count(&count)){
			snprintf(buf, 100, "%d", count);
			mqtt3_db_messages_queue("$SYS/broker/messages/inflight", 2, strlen(buf), (uint8_t *)buf, 1);
		}

		if(!mqtt3_db_client_count(&count)){
			snprintf(buf, 100, "%d", count);
			mqtt3_db_messages_queue("$SYS/broker/clients/total", 2, strlen(buf), (uint8_t *)buf, 1);
		}

		snprintf(buf, 100, "%d", mqtt3_memory_used());
		mqtt3_db_messages_queue("$SYS/broker/heap/current size", 2, strlen(buf), (uint8_t *)buf, 1);

		snprintf(buf, 100, "%lu", mqtt3_net_msgs_total_received());
		mqtt3_db_messages_queue("$SYS/broker/messages/received", 2, strlen(buf), (uint8_t *)buf, 1);
		
		snprintf(buf, 100, "%lu", mqtt3_net_msgs_total_sent());
		mqtt3_db_messages_queue("$SYS/broker/messages/sent", 2, strlen(buf), (uint8_t *)buf, 1);

		snprintf(buf, 100, "%llu", (unsigned long long)mqtt3_net_bytes_total_received());
		mqtt3_db_messages_queue("$SYS/broker/bytes/received", 2, strlen(buf), (uint8_t *)buf, 1);
		
		snprintf(buf, 100, "%llu", (unsigned long long)mqtt3_net_bytes_total_sent());
		mqtt3_db_messages_queue("$SYS/broker/bytes/sent", 2, strlen(buf), (uint8_t *)buf, 1);
		
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
	struct stmt_array *tmp;
	sqlite3_stmt *stmt;

	if(sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK){
		return NULL;
	}

	g_stmt_count++;
	tmp = mqtt3_realloc(g_stmts, sizeof(struct stmt_array)*g_stmt_count);
	if(tmp){
		g_stmts = tmp;
		g_stmts[g_stmt_count-1].stmt = stmt;
	}else{
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
