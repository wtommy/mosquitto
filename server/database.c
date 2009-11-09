#include <sqlite3.h>
#include <stdio.h>

#include <mqtt3.h>

static sqlite3 *db;

int _mqtt3_db_create_tables(void);

int mqtt3_db_open(const char *filename)
{
	if(sqlite3_initialize() != SQLITE_OK){
		return 1;
	}

	if(sqlite3_open(filename, &db) != SQLITE_OK){
		fprintf(stderr, "Error: %s\n", sqlite3_errmsg(db));
		return 1;
	}

	return _mqtt3_db_create_tables();
}

int mqtt3_db_close(void)
{
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
		"id TEXT, "
		"will INTEGER, will_retain INTEGER, will_qos "
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

int mqtt3_db_insert_sub(mqtt3_context *context, uint8_t *sub, int qos)
{
	int rc = 0;
	char query[1024];
	char *errmsg;

	if(!context || !sub) return 1;

	if(snprintf(query, 1024, "INSERT INTO subs (client_id,sub,qos) "
			"SELECT '%s','%s',%d WHERE NOT EXISTS "
			"(SELECT * FROM subs WHERE client_id='%s' AND sub='%s')",
			context->id, sub, qos, context->id, sub) == 1024) return 1;
	
	/* FIXME - sql injection! */
	if(sqlite3_exec(db, query, NULL, NULL, &errmsg) != SQLITE_OK){
		rc = 1;
	}
	if(errmsg){
		fprintf(stderr, "Error: %s\n", errmsg);
		sqlite3_free(errmsg);
	}

	return rc;
}

