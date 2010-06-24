/*
Copyright (c) 2010, Roger Light <roger@atchoo.org>
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

#include <mosquitto.h>
#include <database_mosq.h>

#include <sqlite3.h>

/* Internal function.
 * Finalise all sqlite statements bound to fdb. This must be done before
 * closing the db.
 * See also _mosquitto_db_statement_prepare(db).
 */
void _mosquitto_db_statements_finalize(sqlite3 *db)
{
	sqlite3_stmt *stmt;

	while((stmt = sqlite3_next_stmt(db, NULL))){
		sqlite3_finalize(stmt);
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
sqlite3_stmt *_mosquitto_db_statement_prepare(sqlite3 *db, const char *query)
{
	sqlite3_stmt *stmt;

	if(sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK){
		return NULL;
	}
	return stmt;
}


int _mosquitto_db_transaction_begin(sqlite3 *db)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!stmt){
		stmt = _mosquitto_db_statement_prepare(db, "BEGIN TRANSACTION");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);

	return rc;
}

int _mosquitto_db_transaction_end(sqlite3 *db)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!stmt){
		stmt = _mosquitto_db_statement_prepare(db, "END TRANSACTION");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);

	return rc;
}

int _mosquitto_db_transaction_rollback(sqlite3 *db)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!stmt){
		stmt = _mosquitto_db_statement_prepare(db, "ROLLBACK TRANSACTION");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);

	return rc;
}

