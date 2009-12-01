#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <mqtt3.h>

static sqlite3 *db = NULL;

/* Need to make a struct here to have an array.
 * Don't know size of sqlite3_stmt so can't array it directly.
 */
struct stmt_array {
	sqlite3_stmt *stmt;
};
static int g_stmt_count = 0;
static struct stmt_array *g_stmts = NULL;
static sqlite3_stmt *stmt_sub_search = NULL;

int _mqtt3_db_tables_create(void);
int _mqtt3_db_invalidate_sockets(void);
sqlite3_stmt *_mqtt3_db_statement_prepare(const char *query);
void _mqtt3_db_statements_finalize(void);
int _mqtt3_db_version_check(void);
int _mqtt3_db_transaction_begin(void);
int _mqtt3_db_transaction_end(void);
int _mqtt3_db_transaction_rollback(void);

int mqtt3_db_open(const char *filename)
{
	if(sqlite3_initialize() != SQLITE_OK){
		return 1;
	}

	/* Open without creating first. If found, check for db version.
	 * If not found, open with create.
	 */
	if(sqlite3_open_v2(filename, &db, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK){
		if(_mqtt3_db_version_check()){
			fprintf(stderr, "Error: Invalid database version.\n");
			return 1;
		}
	}else{
		if(sqlite3_open_v2(filename, &db,
				SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK){
			fprintf(stderr, "Error: %s\n", sqlite3_errmsg(db));
			return 1;
		}
		if(_mqtt3_db_tables_create()) return 1;
	}

	if(_mqtt3_db_invalidate_sockets()) return 1;

	return 0;
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
	char *query;

	if(sqlite3_exec(db,
		"CREATE TABLE IF NOT EXISTS config(key TEXT UNIQUE, value TEXT)",
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
		"id TEXT, "
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
		"sub TEXT, qos INTEGER, payloadlen INTEGER, payload BLOB)",
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
		"mid INTEGER, retain INTEGER, sub TEXT, qos INTEGER, payloadlen INTEGER, payload BLOB)",
		NULL, NULL, &errmsg) != SQLITE_OK){

		rc = 1;
	}
	if(errmsg){
		sqlite3_free(errmsg);
		errmsg = NULL;
	}

	return rc;
}

int _mqtt3_db_version_check(void)
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

void _mqtt3_db_statements_finalize(void)
{
	int i;
	for(i=0; i<g_stmt_count; i++){
		if(g_stmts[i].stmt){
			sqlite3_finalize(g_stmts[i].stmt);
		}
	}
	mqtt3_free(g_stmts);
}

int mqtt3_db_client_insert(mqtt3_context *context, int will, int will_retain, int will_qos, const char *will_topic, const char *will_message)
{
	static sqlite3_stmt *stmt = NULL;
	int rc = 0;
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

int mqtt3_db_client_update(mqtt3_context *context, int will, int will_retain, int will_qos, const char *will_topic, const char *will_message)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!context || !context->id) return 1;

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

int mqtt3_db_message_insert(const char *client_id, uint16_t mid, mqtt3_msg_direction dir, mqtt3_msg_status status, int retain, const char *sub, int qos, uint32_t payloadlen, const uint8_t *payload)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!client_id || !sub || !payload) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("INSERT INTO messages "
				"(client_id, timestamp, direction, status, mid, retain, sub, qos, payloadlen, payload) "
				"VALUES (?,?,?,?,?,?,?,?,?,?)");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 2, time(NULL)) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 3, dir) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 4, status) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 5, mid) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 6, retain) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_text(stmt, 7, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 8, qos) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 9, payloadlen) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_blob(stmt, 10, payload, payloadlen, SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

int mqtt3_db_message_update(const char *client_id, uint16_t mid, mqtt3_msg_direction dir, mqtt3_msg_status status)
{
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

int mqtt3_db_messages_queue(const char *sub, int qos, uint32_t payloadlen, const uint8_t *payload, int retain)
{
	int rc = 0;
	char *client_id;
	uint8_t client_qos;
	uint8_t msg_qos;
	uint16_t mid;

	/* Find all clients that subscribe to sub and put messages into the db for them. */
	if(!sub || !payloadlen || !payload) return 1;

	if(retain){
		if(mqtt3_db_retain_insert(sub, qos, payloadlen, payload)) rc = 1;
	}
	if(!mqtt3_db_sub_search_start(sub)){
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
					if(mqtt3_db_message_insert(client_id, mid, md_out, ms_publish, retain, sub, msg_qos, payloadlen, payload)) rc = 1;
					break;
				case 1:
					if(mqtt3_db_message_insert(client_id, mid, md_out, ms_wait_puback, retain, sub, msg_qos, payloadlen, payload)) rc = 1;
					break;
				case 2:
					if(mqtt3_db_message_insert(client_id, mid, md_out, ms_wait_pubrec, retain, sub, msg_qos, payloadlen, payload)) rc = 1;
					break;
			}
			if(client_id) mqtt3_free(client_id);
		}
	}else{
		rc = 1;
	}

	return rc;
}

int mqtt3_db_message_release(const char *client_id, uint16_t mid, mqtt3_msg_direction dir)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;
	uint64_t OID;
	const char *sub;
	int qos;
	int payloadlen;
	const uint8_t *payload;
	int retain;

	if(!client_id) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("SELECT OID,sub,qos,payloadlen,payload,retain FROM messages WHERE client_id=? AND mid=? AND direction=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 2, mid) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 3, dir) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) == SQLITE_ROW){
		sub = (const char *)sqlite3_column_text(stmt, 0);
		qos = sqlite3_column_int(stmt, 1);
		payloadlen = sqlite3_column_int(stmt, 2);
		payload = sqlite3_column_blob(stmt, 3);
		retain = sqlite3_column_int(stmt, 4);
		if(!mqtt3_db_messages_queue(sub, qos, payloadlen, payload, retain)){
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
	uint16_t mid;
	int retain;
	const char *sub;
	int qos;
	uint32_t payloadlen;
	const uint8_t *payload;

	if(!context || !context->id || context->sock == -1) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("SELECT OID,mid,retain,sub,qos,payloadlen,payload FROM messages WHERE status=1 AND direction=1 AND client_id=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, context->id, strlen(context->id), SQLITE_STATIC) == SQLITE_OK){
		/* Only write a single message per call to this function */
		if(sqlite3_step(stmt) == SQLITE_ROW){
			OID = sqlite3_column_int(stmt, 0);
			mid = sqlite3_column_int(stmt, 1);
			retain = sqlite3_column_int(stmt, 2);
			sub = (const char *)sqlite3_column_text(stmt, 3);
			qos = sqlite3_column_int(stmt, 4);
			payloadlen = sqlite3_column_int(stmt, 5);
			payload = sqlite3_column_blob(stmt, 6);
			if(!mqtt3_raw_publish(context, false, qos, retain, mid, sub, payloadlen, payload)){
				mqtt3_db_message_delete_by_oid(OID);
			}
		}
	}else{
		rc = 1;
	}
	sqlite3_reset(stmt);

	return rc;
}

uint16_t mqtt3_db_mid_generate(const char *client_id)
{
	int rc = 0;
	static sqlite3_stmt *stmt_select = NULL;
	static sqlite3_stmt *stmt_update = NULL;
	uint16_t mid = 0;

	if(!client_id) return 0;

	if(!stmt_select){
		stmt_select = _mqtt3_db_statement_prepare("SELECT last_mid FROM clients WHERE client_id=?");
		if(!stmt_select){
			return 0;
		}
	}
	if(!stmt_update){
		stmt_update = _mqtt3_db_statement_prepare("UPDATE clients SET last_mid=? WHERE client_id=?");
		if(!stmt_update){
			return 0;
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
		return 0;
	}
}

int mqtt3_db_outgoing_check(fd_set *writefds, int *sockmax)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;
	int fd;

	if(!writefds) return 1;

	FD_ZERO(writefds);
	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("SELECT sock FROM clients JOIN messages ON clients.id=messages.client_id WHERE messages.status=1 AND messages.direction=1 AND sock<>-1");
		if(!stmt){
			return 1;
		}
	}
	while(sqlite3_step(stmt) == SQLITE_ROW){
		fd = sqlite3_column_int(stmt, 0);
		if(fd > *sockmax) *sockmax = fd;
		FD_SET(fd, writefds);
	}
	sqlite3_reset(stmt);

	return rc;
}

int mqtt3_db_retain_find(const char *sub, int *qos, uint32_t *payloadlen, uint8_t **payload)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;
	const uint8_t *payloadtmp;

	if(!sub) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("SELECT qos,payloadlen,payload FROM retain WHERE sub=?");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;
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

int mqtt3_db_retain_insert(const char *sub, int qos, uint32_t payloadlen, const uint8_t *payload)
{
	int rc = 0;
	static sqlite3_stmt *stmt_update = NULL;
	static sqlite3_stmt *stmt_insert = NULL;

	if(!sub || !payloadlen || !payload) return 1;

	if(!mqtt3_db_retain_find(sub, NULL, NULL, NULL)){
		if(!stmt_update){
			stmt_update = _mqtt3_db_statement_prepare("UPDATE retain SET qos=?,payloadlen=?,payload=? WHERE sub=?");
			if(!stmt_update){
				return 1;
			}
		}
		if(sqlite3_bind_int(stmt_update, 1, qos) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(stmt_update, 2, payloadlen) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_blob(stmt_update, 3, payload, payloadlen, SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_text(stmt_update, 4, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;
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
		if(sqlite3_bind_text(stmt_insert, 1, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(stmt_insert, 2, qos) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_int(stmt_insert, 3, payloadlen) != SQLITE_OK) rc = 1;
		if(sqlite3_bind_blob(stmt_insert, 4, payload, payloadlen, SQLITE_STATIC) != SQLITE_OK) rc = 1;
		if(sqlite3_step(stmt_insert) != SQLITE_DONE) rc = 1;
		sqlite3_reset(stmt_insert);
		sqlite3_clear_bindings(stmt_insert);
	}

	return rc;
}

int mqtt3_db_sub_insert(const char *client_id, const char *sub, int qos)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!client_id || !sub) return 1;

	if(!stmt){
		stmt = _mqtt3_db_statement_prepare("INSERT INTO subs (client_id,sub,qos) "
				"SELECT ?,?,? WHERE NOT EXISTS (SELECT 1 FROM subs WHERE client_id=? AND sub=?)");
		if(!stmt){
			return 1;
		}
	}
	if(sqlite3_bind_text(stmt, 1, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_text(stmt, 2, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_int(stmt, 3, qos) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_text(stmt, 4, client_id, strlen(client_id), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_bind_text(stmt, 5, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE) rc = 1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	return rc;
}

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

int mqtt3_db_sub_search_start(const char *sub)
{
	int rc = 0;
	static sqlite3_stmt *stmt = NULL;

	if(!sub) return 1;

	if(!stmt){
		stmt_sub_search = _mqtt3_db_statement_prepare("SELECT client_id,qos FROM subs where sub=?");
		if(!stmt_sub_search){
			return 1;
		}
	}
	sqlite3_reset(stmt_sub_search);
	sqlite3_clear_bindings(stmt_sub_search);

	if(sqlite3_bind_text(stmt_sub_search, 1, sub, strlen(sub), SQLITE_STATIC) != SQLITE_OK) rc = 1;

	return rc;
}

int mqtt3_db_sub_search_next(char **client_id, uint8_t *qos)
{
	if(sqlite3_step(stmt_sub_search) != SQLITE_ROW){
		return 1;
	}
	*client_id = mqtt3_strdup((char *)sqlite3_column_text(stmt_sub_search, 0));
	*qos = sqlite3_column_int(stmt_sub_search, 1);

	return 0;
}

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

sqlite3_stmt *_mqtt3_db_statement_prepare(const char *query)
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

int _mqtt3_db_transaction_begin(void)
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

int _mqtt3_db_transaction_end(void)
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

int _mqtt3_db_transaction_rollback(void)
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

