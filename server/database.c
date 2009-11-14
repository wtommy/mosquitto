#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

#include <mqtt3.h>

static sqlite3 *db = NULL;
static sqlite3_stmt *sub_search_stmt = NULL;

int _mqtt3_db_create_tables(void);
int _mqtt3_db_invalidate_socks(void);

int mqtt3_db_open(const char *filename)
{
	if(sqlite3_initialize() != SQLITE_OK){
		return 1;
	}

	if(sqlite3_open(filename, &db) != SQLITE_OK){
		fprintf(stderr, "Error: %s\n", sqlite3_errmsg(db));
		return 1;
	}

	if(!_mqtt3_db_create_tables()){
		return _mqtt3_db_invalidate_socks();
	}
	
	return 1;
}

int mqtt3_db_close(void)
{
	if(sub_search_stmt){
		sqlite3_finalize(sub_search_stmt);
	}

	sqlite3_close(db);
	db = NULL;

	sqlite3_shutdown();

	return 0;
}

int _mqtt3_db_create_tables(void)
{
	int rc = 0;
	char *errmsg = NULL;

	if(sqlite3_exec(db,
		"CREATE TABLE IF NOT EXISTS clients("
		"sock INTEGER, "
		"id TEXT, "
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

	if(errmsg) sqlite3_free(errmsg);

	return rc;
}

int mqtt3_db_insert_client(mqtt3_context *context, int will, int will_retain, int will_qos, int8_t *will_topic, int8_t *will_message)
{
	int rc = 0;
	char *query = NULL;
	char *errmsg;

	if(!context) return 1;

	query = sqlite3_mprintf("INSERT INTO clients "
			"(sock,id,will,will_retain,will_qos,will_topic,will_message) "
			"SELECT %d,'%q',%d,%d,%d,'%q','%q' WHERE NOT EXISTS "
			"(SELECT * FROM clients WHERE id='%q')",
			context->sock, context->id, will, will_retain, will_qos, will_topic, will_message, context->id);
	
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

int mqtt3_db_delete_client(mqtt3_context *context)
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

int _mqtt3_db_invalidate_socks(void)
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

int mqtt3_db_insert_sub(mqtt3_context *context, uint8_t *sub, int qos)
{
	int rc = 0;
	char *query = NULL;
	char *errmsg;

	if(!context || !sub) return 1;

	query = sqlite3_mprintf("INSERT INTO subs (client_id,sub,qos) "
			"SELECT '%q','%q',%d WHERE NOT EXISTS "
			"(SELECT * FROM subs WHERE client_id='%q' AND sub='%q')",
			context->id, sub, qos, context->id, sub);
	
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

int mqtt3_db_delete_sub(mqtt3_context *context, uint8_t *sub)
{
	int rc = 0;
	char *query = NULL;
	char *errmsg;

	if(!context || !sub) return 1;

	query = sqlite3_mprintf("DELETE FROM subs WHERE client_id='%q' AND sub='%q'",
			context->id, sub);
	
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

int mqtt3_db_search_sub_start(mqtt3_context *context, uint8_t *sub)
{
	char *query = NULL;
	int rc = 0;

	if(!context || !sub) return 1;

	if(sub_search_stmt){
		sqlite3_finalize(sub_search_stmt);
	}

	query = sqlite3_mprintf("SELECT id,qos FROM subs where sub='%q'", sub);
	
	if(query){
		if(sqlite3_prepare_v2(db, query, -1, &sub_search_stmt, NULL) != SQLITE_OK) rc = 1;
		sqlite3_free(query);
	}else{
		return 1;
	}

	return rc;
}

int mqtt3_db_search_sub_next(uint8_t *client_id, uint8_t *qos)
{
	if(!sub_search_stmt) return 1;
	if(sqlite3_step(sub_search_stmt) != SQLITE_ROW){
		sqlite3_finalize(sub_search_stmt);
		sub_search_stmt = NULL;
		return 1;
	}
	client_id = strdup(sqlite3_column_text(sub_search_stmt, 0));
	*qos = sqlite3_column_int(sub_search_stmt, 1);

	return 0;
}

