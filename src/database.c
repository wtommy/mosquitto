#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <mqtt3.h>

static sqlite3 *db = NULL;
static sqlite3_stmt *stmt_retain_insert = NULL;
static sqlite3_stmt *stmt_retain_update = NULL;
static sqlite3_stmt *stmt_sub_delete = NULL;
static sqlite3_stmt *stmt_sub_insert = NULL;
static sqlite3_stmt *stmt_sub_search = NULL;
static sqlite3_stmt *stmt_subs_delete = NULL;

int _mqtt3_db_tables_create(void);
int _mqtt3_db_invalidate_sockets(void);
int _mqtt3_db_statements_prepare(void);
void _mqtt3_db_statements_finalize(void);

int mqtt3_db_open(const char *filename)
{
	if(sqlite3_initialize() != SQLITE_OK){
		return 1;
	}

	if(sqlite3_open(filename, &db) != SQLITE_OK){
		fprintf(stderr, "Error: %s\n", sqlite3_errmsg(db));
		return 1;
	}

	if(!_mqtt3_db_tables_create()){
		return _mqtt3_db_invalidate_sockets();
	}
	
	_mqtt3_db_statements_prepare();

	return 1;
}

int mqtt3_db_close(void)
{
	_mqtt3_db_statements_finalize();

	sqlite3_close(db);
	db = NULL;

	sqlite3_shutdown();

	return 0;
}

int _mqtt3_db_tables_create(void)
{
	int rc = 0;
	char *errmsg = NULL;

	if(sqlite3_exec(db,
		"CREATE TABLE IF NOT EXISTS clients("
		"sock INTEGER, "
		"id TEXT, "
		"clean_start INTEGER, "
		"will INTEGER, will_retain INTEGER, will_qos INTEGER, "
		"will_topic TEXT, will_message TEXT)",
		NULL, NULL, &errmsg) != SQLITE_OK){

		rc = 1;
	}

	if(errmsg) sqlite3_free(errmsg);

	if(sqlite3_exec(db,
		"CREATE TABLE IF NOT EXISTS subs("
		"client_id TEXT, sub TEXT, qos INTEGER)",
		NULL, NULL, &errmsg) != SQLITE_OK){

		rc = 1;
	}

	if(sqlite3_exec(db,
		"CREATE TABLE IF NOT EXISTS retain("
		"sub TEXT, qos INTEGER, payloadlen INTEGER, payload BLOB)",
		NULL, NULL, &errmsg) != SQLITE_OK){

		rc = 1;
	}

	if(errmsg) sqlite3_free(errmsg);

	return rc;
}

int _mqtt3_db_statements_prepare(void)
{
	int rc = 0;

	if(sqlite3_prepare_v2(db, "INSERT INTO retain VALUES (?,?,?,?)", -1, &stmt_retain_insert, NULL) != SQLITE_OK) rc = 1;
	if(sqlite3_prepare_v2(db, "UPDATE retain SET qos=?,payloadlen=?,payload=? WHERE sub=?", -1, &stmt_retain_update, NULL) != SQLITE_OK) rc = 1;
	if(sqlite3_prepare_v2(db, "DELETE FROM subs WHERE client_id=? AND sub=?", -1, &stmt_sub_delete, NULL) != SQLITE_OK) rc = 1;
	if(sqlite3_prepare_v2(db, "INSERT INTO subs (client_id,sub,qos) "
			"SELECT ?,?,? WHERE NOT EXISTS (SELECT * FROM subs WHERE client_id=? AND sub=?)",
			-1, &stmt_sub_insert, NULL) != SQLITE_OK) rc = 1;
	if(sqlite3_prepare_v2(db, "SELECT id,qos FROM subs where sub=?", -1, &stmt_sub_search, NULL) != SQLITE_OK) rc = 1;
	if(sqlite3_prepare_v2(db, "DELETE FROM subs WHERE client_id=?", -1, &stmt_subs_delete, NULL) != SQLITE_OK) rc = 1;

	return rc;
}

void _mqtt3_db_statements_finalize(void)
{
	if(stmt_retain_insert) sqlite3_finalize(stmt_retain_insert);
	if(stmt_retain_update) sqlite3_finalize(stmt_retain_update);
	if(stmt_sub_insert) sqlite3_finalize(stmt_sub_insert);
	if(stmt_sub_delete) sqlite3_finalize(stmt_subs_delete);
	if(stmt_sub_search) sqlite3_finalize(stmt_sub_search);
	if(stmt_subs_delete) sqlite3_finalize(stmt_subs_delete);
}

int mqtt3_db_client_insert(mqtt3_context *context, int will, int will_retain, int will_qos, const char *will_topic, const char *will_message)
{
	int rc = 0;
	char *query = NULL;
	char *errmsg;
	int oldsock;

	if(!context) return 1;

	if(!mqtt3_db_client_find_socket(context->id, &oldsock)){
		if(oldsock == -1){
			/* Client is reconnecting after a disconnect */
		}else if(oldsock != context->sock){
			/* Client is already connected, disconnect old version */
			fprintf(stderr, "Client %s already connected, closing old connection.\n", context->id);
			close(oldsock);
		}
		mqtt3_db_client_update(context, will, will_retain, will_qos, will_topic, will_message);
	}else{
		query = sqlite3_mprintf("INSERT INTO clients "
				"(sock,id,clean_start,will,will_retain,will_qos,will_topic,will_message) "
				"SELECT %d,'%q',%d,%d,%d,%d,'%q','%q' WHERE NOT EXISTS "
				"(SELECT * FROM clients WHERE id='%q')",
				context->sock, context->id, context->clean_start, will, will_retain, will_qos, will_topic, will_message, context->id);
	
		if(query){
			if(sqlite3_exec(db, query, NULL, NULL, &errmsg) != SQLITE_OK){
				rc = 1;
			}
			sqlite3_free(query);
			if(errmsg){
				fprintf(stderr, "Error: %s\n", errmsg);
				sqlite3_free(errmsg);
			}
		}else{
			rc = 1;
		}
	}
	return rc;
}

int mqtt3_db_client_update(mqtt3_context *context, int will, int will_retain, int will_qos, const char *will_topic, const char *will_message)
{
	int rc = 0;
	char *query = NULL;
	char *errmsg;

	if(!context) return 1;

	query = sqlite3_mprintf("UPDATE clients SET "
			"sock=%d,clean_start=%d,will=%d,will_retain=%d,will_qos=%d,"
			"will_topic='%q',will_message='%q' "
			"WHERE id='%q'",
			context->sock, context->clean_start, will, will_retain, will_qos,
			will_topic, will_message, context->id);
	
	if(query){
		if(sqlite3_exec(db, query, NULL, NULL, &errmsg) != SQLITE_OK){
			rc = 1;
		}
		sqlite3_free(query);
		if(errmsg){
			fprintf(stderr, "Error: %s\n", errmsg);
			sqlite3_free(errmsg);
		}
	}else{
		return 1;
	}
	return rc;
}

int mqtt3_db_client_delete(mqtt3_context *context)
{
	int rc = 0;
	char *query = NULL;
	char *errmsg;

	if(!context || !(context->id)) return 1;

	query = sqlite3_mprintf("DELETE FROM clients WHERE client_id='%q'", context->id);
	
	if(query){
		if(sqlite3_exec(db, query, NULL, NULL, &errmsg) != SQLITE_OK){
			rc = 1;
		}
		sqlite3_free(query);
		if(errmsg){
			fprintf(stderr, "Error: %s\n", errmsg);
			sqlite3_free(errmsg);
		}
	}else{
		return 1;
	}

	return rc;
}

int mqtt3_db_client_find_socket(const char *client_id, int *sock)
{
	int rc = 0;
	char *query = NULL;
	sqlite3_stmt *stmt;

	if(!client_id || !sock) return 1;

	query = sqlite3_mprintf("SELECT sock FROM clients WHERE id='%q'", client_id);

	if(query){
		if(sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK) rc = 1;
		sqlite3_free(query);

		if(sqlite3_step(stmt) == SQLITE_ROW){
			*sock = sqlite3_column_int(stmt, 0);
			sqlite3_finalize(stmt);
		}else{
			rc = 1;
		}
	}else{
		return 1;
	}

	return rc;
}

int _mqtt3_db_invalidate_sockets(void)
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
			fprintf(stderr, "Error: %s\n", errmsg);
			sqlite3_free(errmsg);
		}
	}else{
		return 1;
	}

	return rc;
}

int mqtt3_db_client_invalidate_socket(const char *client_id, int sock)
{
	int rc = 0;
	char *query = NULL;
	char *errmsg;

	if(!db || !client_id) return 1;

	query = sqlite3_mprintf("UPDATE clients SET sock=-1 "
			"WHERE id='%q' AND sock=%d",
			client_id, sock);
	if(query){
		if(sqlite3_exec(db, query, NULL, NULL, &errmsg) != SQLITE_OK){
			rc = 1;
		}
		sqlite3_free(query);
		if(errmsg){
			fprintf(stderr, "Error: %s\n", errmsg);
			sqlite3_free(errmsg);
		}
	}else{
		return 1;
	}

	return rc;
}

int mqtt3_db_retain_insert(const char *sub, int qos, uint32_t payloadlen, uint8_t *payload)
{
	int rc = 0;

	if(!sub || !payloadlen || !payload) return 1;

	if(sqlite3_bind_text(stmt_retain_insert, 0, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt_retain_insert, 1, qos) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt_retain_insert, 2, payloadlen) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_blob(stmt_retain_insert, 3, payload, payloadlen, SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt_retain_insert) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt_retain_insert);
	sqlite3_clear_bindings(stmt_retain_insert);

	return rc;
}

int mqtt3_db_sub_insert(mqtt3_context *context, const char *sub, int qos)
{
	int rc = 0;

	if(!context || !sub) return 1;

	if(sqlite3_bind_text(stmt_sub_insert, 0, context->id, strlen(context->id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_text(stmt_sub_insert, 1, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt_sub_insert, 2, qos) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_text(stmt_sub_insert, 3, context->id, strlen(context->id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_text(stmt_sub_insert, 4, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt_sub_insert) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt_sub_insert);
	sqlite3_clear_bindings(stmt_sub_insert);

	return rc;
}

int mqtt3_db_sub_delete(mqtt3_context *context, const char *sub)
{
	int rc = 0;

	if(!context || !sub) return 1;

	if(sqlite3_bind_text(stmt_sub_delete, 0, context->id, strlen(context->id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_text(stmt_sub_delete, 1, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt_sub_delete) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt_sub_delete);
	sqlite3_clear_bindings(stmt_sub_delete);

	return rc;
}

int mqtt3_db_sub_search_start(const char *sub)
{
	int rc = 0;

	if(!sub) return 1;

	sqlite3_reset(stmt_sub_search);
	sqlite3_clear_bindings(stmt_sub_search);

	if(sqlite3_bind_text(stmt_sub_search, 0, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;

	return rc;
}

int mqtt3_db_sub_search_next(char *client_id, uint8_t *qos)
{
	if(sqlite3_step(stmt_sub_search) != SQLITE_ROW){
		return 1;
	}
	client_id = mqtt3_strdup((char *)sqlite3_column_text(stmt_sub_search, 0));
	*qos = sqlite3_column_int(stmt_sub_search, 1);

	return 0;
}

int mqtt3_db_subs_clean_start(mqtt3_context *context)
{
	int rc = 0;

	if(!context || !(context->id)) return 1;

	if(sqlite3_bind_text(stmt_subs_delete, 0, context->id, strlen(context->id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt_sub_insert) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt_retain_insert);
	sqlite3_clear_bindings(stmt_retain_insert);

	return rc;
}

