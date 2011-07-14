/*
Copyright (c) 2010,2011 Roger Light <roger@atchoo.org>
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

#include <config.h>

#ifdef WITH_PERSISTENCE

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <memory_mosq.h>
#include <mqtt3.h>

/* DB read/write */
const unsigned char magic[15] = {0x00, 0xB5, 0x00, 'm','o','s','q','u','i','t','t','o',' ','d','b'};
#define DB_CHUNK_CFG 1
#define DB_CHUNK_MSG_STORE 2
#define DB_CHUNK_CLIENT_MSG 3
#define DB_CHUNK_RETAIN 4
#define DB_CHUNK_SUB 5
/* End DB read/write */

static int _db_restore_sub(mosquitto_db *db, const char *client_id, const char *sub, int qos);

static mqtt3_context *_db_find_or_add_context(mosquitto_db *db, const char *client_id)
{
	mqtt3_context *context;
	mqtt3_context **tmp_contexts;
	int i;

	context = NULL;
	for(i=0; i<db->context_count; i++){
		if(db->contexts[i] && !strcmp(db->contexts[i]->core.id, client_id)){
			context = db->contexts[i];
			break;
		}
	}
	if(!context){
		context = mqtt3_context_init(-1);

		for(i=0; i<db->context_count; i++){
			if(!db->contexts[i]){
				db->contexts[i] = context;
				break;
			}
		}
		if(i==db->context_count){
			db->context_count++;
			tmp_contexts = _mosquitto_realloc(db->contexts, sizeof(mqtt3_context*)*db->context_count);
			if(tmp_contexts){
				db->contexts = tmp_contexts;
				db->contexts[db->context_count-1] = context;
			}else{
				return NULL;
			}
		}
		context->core.id = _mosquitto_strdup(client_id);
	}
	return context;
}

static int mqtt3_db_client_messages_write(mosquitto_db *db, int db_fd, mqtt3_context *context)
{
	uint32_t length;
	dbid_t i64temp;
	uint16_t i16temp, slen;
	uint8_t i8temp;
	mosquitto_client_msg *cmsg;

	assert(db);
	assert(db_fd >= 0);
	assert(context);

#define write_e(a, b, c) if(write(a, b, c) != c){ return 1; }

	cmsg = context->msgs;
	while(cmsg){
		slen = strlen(context->core.id);

		length = htonl(sizeof(dbid_t) + sizeof(uint16_t) + sizeof(uint8_t) +
				sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t) +
				sizeof(uint8_t) + 2+slen);

		i16temp = htons(DB_CHUNK_CLIENT_MSG);
		write_e(db_fd, &i16temp, sizeof(uint16_t));
		write_e(db_fd, &length, sizeof(uint32_t));

		i16temp = htons(slen);
		write_e(db_fd, &i16temp, sizeof(uint16_t));
		write_e(db_fd, context->core.id, slen);

		i64temp = cmsg->store->db_id;
		write_e(db_fd, &i64temp, sizeof(dbid_t));

		i16temp = htons(cmsg->mid);
		write_e(db_fd, &i16temp, sizeof(uint16_t));

		i8temp = (uint8_t )cmsg->qos;
		write_e(db_fd, &i8temp, sizeof(uint8_t));

		i8temp = (uint8_t )cmsg->retain;
		write_e(db_fd, &i8temp, sizeof(uint8_t));

		i8temp = (uint8_t )cmsg->direction;
		write_e(db_fd, &i8temp, sizeof(uint8_t));

		i8temp = (uint8_t )cmsg->state;
		write_e(db_fd, &i8temp, sizeof(uint8_t));

		i8temp = (uint8_t )cmsg->dup;
		write_e(db_fd, &i8temp, sizeof(uint8_t));

		cmsg = cmsg->next;
	}

#undef write_e

	return MOSQ_ERR_SUCCESS;
}


static int mqtt3_db_message_store_write(mosquitto_db *db, int db_fd)
{
	uint32_t length;
	dbid_t i64temp;
	uint32_t i32temp;
	uint16_t i16temp, slen;
	uint8_t i8temp;
	struct mosquitto_msg_store *stored;

	assert(db);
	assert(db_fd >= 0);

#define write_e(a, b, c) if(write(a, b, c) != c){ return 1; }

	stored = db->msg_store;
	while(stored){
		length = htonl(sizeof(dbid_t) + 2+strlen(stored->source_id) +
				sizeof(uint16_t) + sizeof(uint16_t) +
				2+strlen(stored->msg.topic) + sizeof(uint32_t) +
				stored->msg.payloadlen + sizeof(uint8_t) + sizeof(uint8_t));

		i16temp = htons(DB_CHUNK_MSG_STORE);
		write_e(db_fd, &i16temp, sizeof(uint16_t));
		write_e(db_fd, &length, sizeof(uint32_t));

		i64temp = stored->db_id;
		write_e(db_fd, &i64temp, sizeof(dbid_t));

		slen = strlen(stored->source_id);
		i16temp = htons(slen);
		write_e(db_fd, &i16temp, sizeof(uint16_t));
		if(slen){
			write_e(db_fd, stored->source_id, slen);
		}

		i16temp = htons(stored->source_mid);
		write_e(db_fd, &i16temp, sizeof(uint16_t));

		i16temp = htons(stored->msg.mid);
		write_e(db_fd, &i16temp, sizeof(uint16_t));

		slen = strlen(stored->msg.topic);
		i16temp = htons(slen);
		write_e(db_fd, &i16temp, sizeof(uint16_t));
		write_e(db_fd, stored->msg.topic, slen);

		i8temp = (uint8_t )stored->msg.qos;
		write_e(db_fd, &i8temp, sizeof(uint8_t));

		i8temp = (uint8_t )stored->msg.retain;
		write_e(db_fd, &i8temp, sizeof(uint8_t));

		i32temp = htonl(stored->msg.payloadlen);
		write_e(db_fd, &i32temp, sizeof(uint32_t));
		if(stored->msg.payloadlen){
			write_e(db_fd, stored->msg.payload, stored->msg.payloadlen);
		}

		stored = stored->next;
	}

#undef write_e

	return MOSQ_ERR_SUCCESS;
}

static int mqtt3_db_client_write(mosquitto_db *db, int db_fd)
{
	int i;
	mqtt3_context *context;

	assert(db);
	assert(db_fd >= 0);

	for(i=0; i<db->context_count; i++){
		context = db->contexts[i];
		if(context && context->core.clean_session == false){
			if(mqtt3_db_client_messages_write(db, db_fd, context)) return 1;
		}
	}

	return MOSQ_ERR_SUCCESS;
}

static int _db_subs_retain_write(mosquitto_db *db, int db_fd, struct _mosquitto_subhier *node, const char *topic)
{
	struct _mosquitto_subhier *subhier;
	struct _mosquitto_subleaf *sub;
	char *thistopic;
	uint32_t length;
	uint16_t i16temp;
	dbid_t i64temp;
	int slen;

	slen = strlen(topic) + strlen(node->topic) + 2;
	thistopic = _mosquitto_malloc(sizeof(char)*slen);
	if(!thistopic) return MOSQ_ERR_NOMEM;
	if(strlen(topic)){
		snprintf(thistopic, slen, "%s/%s", topic, node->topic);
	}else{
		snprintf(thistopic, slen, "%s", node->topic);
	}

#define write_e(a, b, c) if(write(a, b, c) != c){ return 1; }
	sub = node->subs;
	while(sub){
		if(sub->context->core.clean_session == false){
			length = htonl(2+strlen(sub->context->core.id) + 2+strlen(thistopic) + sizeof(uint8_t));

			i16temp = htons(DB_CHUNK_SUB);
			write_e(db_fd, &i16temp, sizeof(uint16_t));
			write_e(db_fd, &length, sizeof(uint32_t));

			slen = strlen(sub->context->core.id);
			i16temp = htons(slen);
			write_e(db_fd, &i16temp, sizeof(uint16_t));
			write_e(db_fd, sub->context->core.id, slen);

			slen = strlen(thistopic);
			i16temp = htons(slen);
			write_e(db_fd, &i16temp, sizeof(uint16_t));
			write_e(db_fd, thistopic, slen);

			write_e(db_fd, &sub->qos, sizeof(uint8_t));
		}
		sub = sub->next;
	}
	if(node->retained){
		length = htonl(sizeof(dbid_t));

		i16temp = htons(DB_CHUNK_RETAIN);
		write_e(db_fd, &i16temp, sizeof(uint16_t));
		write_e(db_fd, &length, sizeof(uint32_t));

		i64temp = node->retained->db_id;
		write_e(db_fd, &i64temp, sizeof(dbid_t));
	}
#undef write_e

	subhier = node->children;
	while(subhier){
		_db_subs_retain_write(db, db_fd, subhier, thistopic);
		subhier = subhier->next;
	}
	_mosquitto_free(thistopic);
	return MOSQ_ERR_SUCCESS;
}

static int mqtt3_db_subs_retain_write(mosquitto_db *db, int db_fd)
{
	struct _mosquitto_subhier *subhier;

	subhier = db->subs.children;
	while(subhier){
		_db_subs_retain_write(db, db_fd, subhier, "");
		subhier = subhier->next;
	}
	
	return MOSQ_ERR_SUCCESS;
}

int mqtt3_db_backup(mosquitto_db *db, bool cleanup, bool shutdown)
{
	int rc = 0;
	int db_fd;
	uint32_t db_version = htonl(MOSQ_DB_VERSION);
	uint32_t crc = htonl(0);
	dbid_t i64temp;
	uint32_t i32temp;
	uint16_t i16temp;
	uint8_t i8temp;

	if(!db || !db->config || !db->config->persistence_filepath) return MOSQ_ERR_INVAL;
	mqtt3_log_printf(MOSQ_LOG_INFO, "Saving in-memory database to %s.", db->config->persistence_filepath);
	if(cleanup){
		mqtt3_db_store_clean(db);
	}

	db_fd = open(db->config->persistence_filepath, O_WRONLY|O_CREAT|O_TRUNC, S_IWUSR|S_IRUSR);
	if(db_fd < 0){
		goto error;
	}

#define write_e(a, b, c) if(write(a, b, c) != c){ goto error; }
	/* Header */
	write_e(db_fd, magic, 15);
	write_e(db_fd, &crc, sizeof(uint32_t));
	write_e(db_fd, &db_version, sizeof(uint32_t));

	/* FIXME - what more config is needed? */
	/* DB config */
	i16temp = htons(DB_CHUNK_CFG);
	write_e(db_fd, &i16temp, sizeof(uint16_t));
	/* chunk length */
	i32temp = htonl(sizeof(dbid_t) + sizeof(uint8_t) + sizeof(uint8_t));
	write_e(db_fd, &i32temp, sizeof(uint32_t));
	/* db written at broker shutdown or not */
	i8temp = shutdown;
	write_e(db_fd, &i8temp, sizeof(uint8_t));
	i8temp = sizeof(dbid_t);
	write_e(db_fd, &i8temp, sizeof(uint8_t));
	/* last db mid */
	i64temp = db->last_db_id;
	write_e(db_fd, &i64temp, sizeof(dbid_t));
#undef write_e

	if(mqtt3_db_message_store_write(db, db_fd)){
		goto error;
	}

	mqtt3_db_client_write(db, db_fd);
	mqtt3_db_subs_retain_write(db, db_fd);

	/* FIXME - needs implementing */
	close(db_fd);
	return rc;
error:
	mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	if(db_fd >= 0) close(db_fd);
	return 1;
}

static int _db_client_msg_restore(mosquitto_db *db, const char *client_id, uint16_t mid, uint8_t qos, uint8_t retain, uint8_t direction, uint8_t state, uint8_t dup, uint64_t store_id)
{
	mosquitto_client_msg *cmsg, *tail;
	struct mosquitto_msg_store *store;
	mqtt3_context *context;

	cmsg = _mosquitto_calloc(1, sizeof(mosquitto_client_msg));
	if(!cmsg){
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory.");
		return MOSQ_ERR_NOMEM;
	}

	cmsg->store = NULL;
	cmsg->mid = mid;
	cmsg->qos = qos;
	cmsg->retain = retain;
	cmsg->direction = direction;
	cmsg->state = state;
	cmsg->dup = dup;

	store = db->msg_store;
	while(store){
		if(store->db_id == store_id){
			cmsg->store = store;
			break;
		}
		store = store->next;
	}
	if(!cmsg->store){
		_mosquitto_free(cmsg);
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error restoring persistent database, message store corrupt.");
		return 1;
	}
	context = _db_find_or_add_context(db, client_id);
	if(!context){
		_mosquitto_free(cmsg);
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error restoring persistent database, message store corrupt.");
		return 1;
	}
	if(context->msgs){
		tail = context->msgs;
		while(tail->next){
			tail = tail->next;
		}
		tail->next = cmsg;
	}else{
		context->msgs = cmsg;
	}
	cmsg->next = NULL;

	return MOSQ_ERR_SUCCESS;
}

static int _db_client_msg_chunk_restore(mosquitto_db *db, int db_fd)
{
	dbid_t i64temp, store_id;
	uint16_t i16temp, slen, mid;
	uint8_t qos, retain, direction, state, dup;
	char *client_id = NULL;
	int rc;

#define read_e(a, b, c) if(read(a, b, c) != c){ goto error; }
	read_e(db_fd, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	if(!slen){
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Corrupt persistent database.");
		close(db_fd);
		return 1;
	}
	client_id = _mosquitto_calloc(slen+1, sizeof(char));
	if(!client_id){
		close(db_fd);
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory.");
		return MOSQ_ERR_NOMEM;
	}
	read_e(db_fd, client_id, slen);

	read_e(db_fd, &i64temp, sizeof(dbid_t));
	store_id = i64temp;

	read_e(db_fd, &i16temp, sizeof(uint16_t));
	mid = ntohs(i16temp);

	read_e(db_fd, &qos, sizeof(uint8_t));
	read_e(db_fd, &retain, sizeof(uint8_t));
	read_e(db_fd, &direction, sizeof(uint8_t));
	read_e(db_fd, &state, sizeof(uint8_t));
	read_e(db_fd, &dup, sizeof(uint8_t));
#undef read_e

	rc = _db_client_msg_restore(db, client_id, mid, qos, retain, direction, state, dup, store_id);
	_mosquitto_free(client_id);

	return rc;
error:
	mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	if(db_fd >= 0) close(db_fd);
	if(client_id) _mosquitto_free(client_id);
	return 1;
}

static int _db_msg_store_chunk_restore(mosquitto_db *db, int db_fd)
{
	dbid_t i64temp, store_id;
	uint32_t i32temp, payloadlen;
	uint16_t i16temp, slen, source_mid, mid;
	uint8_t qos, retain, *payload = NULL;
	char *source_id = NULL;
	char *topic = NULL;
	int rc = 0;
	struct mosquitto_msg_store *stored = NULL;

#define read_e(a, b, c) if(read(a, b, c) != c){ goto error; }
	read_e(db_fd, &i64temp, sizeof(dbid_t));
	store_id = i64temp;

	read_e(db_fd, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	if(slen){
		source_id = _mosquitto_calloc(slen+1, sizeof(char));
		if(!source_id){
			close(db_fd);
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory.");
			return MOSQ_ERR_NOMEM;
		}
		if(read(db_fd, source_id, slen) != slen){
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
			close(db_fd);
			_mosquitto_free(source_id);
			return 1;
		}
	}
	read_e(db_fd, &i16temp, sizeof(uint16_t));
	source_mid = ntohs(i16temp);

	read_e(db_fd, &i16temp, sizeof(uint16_t));
	mid = ntohs(i16temp);

	read_e(db_fd, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	if(slen){
		topic = _mosquitto_calloc(slen+1, sizeof(char));
		if(!topic){
			close(db_fd);
			_mosquitto_free(source_id);
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory.");
			return MOSQ_ERR_NOMEM;
		}
		if(read(db_fd, topic, slen) != slen){
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
			close(db_fd);
			_mosquitto_free(source_id);
			_mosquitto_free(topic);
			return 1;
		}
	}else{
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid msg_store chunk when restoring persistent database.");
		close(db_fd);
		_mosquitto_free(source_id);
		return 1;
	}
	read_e(db_fd, &qos, sizeof(uint8_t));
	read_e(db_fd, &retain, sizeof(uint8_t));
	
	read_e(db_fd, &i32temp, sizeof(uint32_t));
	payloadlen = ntohl(i32temp);

	if(payloadlen){
		payload = _mosquitto_malloc(payloadlen);
		if(!payload){
			close(db_fd);
			_mosquitto_free(source_id);
			_mosquitto_free(topic);
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory.");
			return MOSQ_ERR_NOMEM;
		}
		if(read(db_fd, payload, payloadlen) != payloadlen){
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
			close(db_fd);
			_mosquitto_free(source_id);
			_mosquitto_free(topic);
			_mosquitto_free(payload);
			return 1;
		}
	}

#undef read_e
	rc = mqtt3_db_message_store(db, source_id, source_mid, topic, qos, payloadlen, payload, retain, &stored, store_id);
	_mosquitto_free(source_id);
	_mosquitto_free(topic);
	_mosquitto_free(payload);

	return rc;
error:
	mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	if(db_fd >= 0) close(db_fd);
	if(source_id) _mosquitto_free(source_id);
	if(topic) _mosquitto_free(topic);
	return 1;
}

static int _db_retain_chunk_restore(mosquitto_db *db, int db_fd)
{
	dbid_t i64temp, store_id;
	struct mosquitto_msg_store *store;

	if(read(db_fd, &i64temp, sizeof(dbid_t)) != sizeof(dbid_t)){
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
		close(db_fd);
		return 1;
	}
	store_id = i64temp;
	store = db->msg_store;
	while(store){
		if(store->db_id == store_id){
			mqtt3_sub_search(db, &db->subs, NULL, store->msg.topic, store->msg.qos, store->msg.retain, store);
			break;
		}
		store = store->next;
	}
	if(!store){
		close(db_fd);
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error restoring persistent database, message store corrupt.");
		return 1;
	}
	return MOSQ_ERR_SUCCESS;
}

static int _db_sub_chunk_restore(mosquitto_db *db, int db_fd)
{
	uint16_t i16temp, slen;
	uint8_t qos;
	char *client_id;
	char *topic;
	int rc = 0;

#define read_e(a, b, c) if(read(a, b, c) != c){ goto error; }
	read_e(db_fd, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	client_id = _mosquitto_calloc(slen+1, sizeof(char));
	if(!client_id){
		close(db_fd);
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory.");
		return MOSQ_ERR_NOMEM;
	}
	read_e(db_fd, client_id, slen);
	read_e(db_fd, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	topic = _mosquitto_calloc(slen+1, sizeof(char));
	if(!topic){
		close(db_fd);
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory.");
		_mosquitto_free(client_id);
		return MOSQ_ERR_NOMEM;
	}
	read_e(db_fd, topic, slen);
	read_e(db_fd, &qos, sizeof(uint8_t));
	if(_db_restore_sub(db, client_id, topic, qos)){
		rc = 1;
	}
#undef read_e
	_mosquitto_free(client_id);
	_mosquitto_free(topic);

	return rc;
error:
	mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	if(db_fd >= 0) close(db_fd);
	return 1;
}

int mqtt3_db_restore(mosquitto_db *db)
{
	int fd;
	char header[15];
	int rc = 0;
	uint32_t crc, db_version;
	dbid_t i64temp;
	uint32_t i32temp, length;
	uint16_t i16temp, chunk;
	uint8_t i8temp;
	ssize_t rlen;

	assert(db);
	assert(db->config);
	assert(db->config->persistence_filepath);

#define read_e(a, b, c) if(read(a, b, c) != c){ goto error; }
	fd = open(db->config->persistence_filepath, O_RDONLY);
	if(fd < 0) return MOSQ_ERR_SUCCESS;
	read_e(fd, &header, 15);
	if(!memcmp(header, magic, 15)){
		// Restore DB as normal
		read_e(fd, &crc, sizeof(uint32_t));
		read_e(fd, &i32temp, sizeof(uint32_t));
		db_version = ntohl(i32temp);
		if(db_version != MOSQ_DB_VERSION && db_version != 0){
			close(fd);
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Unsupported persistent database format version %d (need version %d).", db_version, MOSQ_DB_VERSION);
			return 1;
		}

		while(rlen = read(fd, &i16temp, sizeof(uint16_t)), rlen == sizeof(uint16_t)){
			chunk = ntohs(i16temp);
			read_e(fd, &i32temp, sizeof(uint32_t));
			length = ntohl(i32temp);
			switch(chunk){
				case DB_CHUNK_CFG:
					read_e(fd, &i8temp, sizeof(uint8_t)); // shutdown
					read_e(fd, &i8temp, sizeof(uint8_t)); // sizeof(dbid_t)
					if(i8temp != sizeof(dbid_t)){
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Incompatible database configuration (dbid size is %d bytes, expected %d)",
								i8temp, sizeof(dbid_t));
						close(fd);
						return 1;
					}
					read_e(fd, &i64temp, sizeof(dbid_t));
					db->last_db_id = i64temp;
					break;

				case DB_CHUNK_MSG_STORE:
					if(_db_msg_store_chunk_restore(db, fd)) return 1;
					break;

				case DB_CHUNK_CLIENT_MSG:
					if(_db_client_msg_chunk_restore(db, fd)) return 1;
					break;

				case DB_CHUNK_RETAIN:
					if(_db_retain_chunk_restore(db, fd)) return 1;
					break;

				case DB_CHUNK_SUB:
					if(_db_sub_chunk_restore(db, fd)) return 1;
					break;

				default:
					mqtt3_log_printf(MOSQ_LOG_WARNING, "Warning: Unsupported chunk \"%d\" in persistent database file. Ignoring.", chunk);
					lseek(fd, length, SEEK_CUR);
					break;
			}
		}
		if(rlen < 0) goto error;
	}else{
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Unable to restore persistent database. Unrecognised file format.");
		rc = 1;
	}
#undef read_e

	close(fd);

	return rc;
error:
	mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	if(fd >= 0) close(fd);
	return 1;
}

static int _db_restore_sub(mosquitto_db *db, const char *client_id, const char *sub, int qos)
{
	mqtt3_context *context;

	assert(db);
	assert(client_id);
	assert(sub);

	context = _db_find_or_add_context(db, client_id);
	if(!context) return 1;
	return mqtt3_sub_add(context, sub, qos, &db->subs);
}

#endif
