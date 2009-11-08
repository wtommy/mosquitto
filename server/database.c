#include <sqlite3.h>
#include <stdio.h>

static sqlite3 *db;

int _mqtt_db_create_tables(void);

int mqtt_db_open(const char *filename)
{
	if(sqlite3_open(filename, &db) != SQLITE_OK){
		fprintf(stderr, "Error: %s\n", sqlite3_errmsg(db));
		return 1;
	}

	return _mqtt_db_create_tables();
}

int mqtt_db_close(void)
{
	sqlite3_close(db);
	db = NULL;

	return 0;
}

int _mqtt_db_create_tables(void)
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


