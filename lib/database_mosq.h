#ifndef _MOSQ_DATABASE_H_
#define _MOSQ_DATABASE_H_

#include <sqlite3.h>

int _mosquitto_db_close(sqlite3 *db);

void _mosquitto_db_statements_finalize(sqlite3 *db);
sqlite3_stmt *_mosquitto_db_statement_prepare(sqlite3 *db, const char *query);
int _mosquitto_db_transaction_begin(sqlite3 *db);
int _mosquitto_db_transaction_end(sqlite3 *db);
int _mosquitto_db_transaction_rollback(sqlite3 *db);

#endif
