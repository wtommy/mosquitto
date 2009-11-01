#include <sqlite3.h>
#include <stdio.h>

static sqlite3 *db;

int mqtt_open_db(const char *filename)
{
	if(sqlite3_open(filename, &db) != SQLITE_OK){
		fprintf(stderr, "Error: %s\n", sqlite3_errmsg(db));
		return 1;
	}

	return 0;
}

int mqtt_close_db(void)
{
	sqlite3_close(db);
	db = NULL;

	return 0;
}

