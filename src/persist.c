/*
Copyright (c) 2010 Roger Light <roger@atchoo.org>
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

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#include <config.h>
#include <mqtt3.h>

/* DB read/write */
const unsigned char magic[15] = {0x00, 0xB5, 0x00, 'm','o','s','q','u','i','t','t','o',' ','d','b'};
#define DB_CHUNK_CFG 1
#define DB_CHUNK_MSG_STORE 2
#define DB_CHUNK_CLIENT_MSG 3
/* End DB read/write */

static int mqtt3_db_client_messages_write(mosquitto_db *db, int db_fd, mqtt3_context *context)
{
	uint32_t length;
	uint64_t i64temp;
	uint16_t i16temp;
	uint8_t i8temp;
	mosquitto_client_msg *cmsg;

	assert(db);
	assert(db_fd >= 0);
	assert(context);

#define write_e(a, b, c) if(write(a, b, c) != c){ return 1; }

	cmsg = context->msgs;
	while(cmsg){
		length = htonl(sizeof(uint64_t) + sizeof(uint16_t) + sizeof(uint8_t) +
				sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t) +
				sizeof(uint8_t));

		i16temp = htons(DB_CHUNK_CLIENT_MSG);
		write_e(db_fd, &i16temp, sizeof(uint16_t));
		write_e(db_fd, &length, sizeof(uint32_t));

		i64temp = htobe64(cmsg->store->db_id);
		write_e(db_fd, &i64temp, sizeof(uint64_t));

		i16temp = htons(cmsg->mid);
		write_e(db_fd, &i16temp, sizeof(uint16_t));

		i8temp = (uint8_t )cmsg->qos;
		write_e(db_fd, &i8temp, sizeof(uint16_t));

		i8temp = (uint8_t )cmsg->retain;
		write_e(db_fd, &i8temp, sizeof(uint16_t));

		i8temp = (uint8_t )cmsg->direction;
		write_e(db_fd, &i8temp, sizeof(uint16_t));

		i8temp = (uint8_t )cmsg->state;
		write_e(db_fd, &i8temp, sizeof(uint16_t));

		i8temp = (uint8_t )cmsg->dup;
		write_e(db_fd, &i8temp, sizeof(uint16_t));

		cmsg = cmsg->next;
	}

#undef write_e

	return 0;
}


static int mqtt3_db_message_store_write(mosquitto_db *db, int db_fd)
{
	uint32_t length;
	uint64_t i64temp;
	uint32_t i32temp;
	uint16_t i16temp;
	uint8_t i8temp;
	struct mosquitto_msg_store *stored;

	assert(db);
	assert(db_fd >= 0);

#define write_e(a, b, c) if(write(a, b, c) != c){ return 1; }

	stored = db->msg_store;
	while(stored){
		length = htonl(sizeof(uint64_t) + 2+strlen(stored->source_id) +
				sizeof(uint16_t) + sizeof(uint16_t) +
				2+strlen(stored->msg.topic) + sizeof(uint32_t) +
				stored->msg.payloadlen + sizeof(uint8_t) + sizeof(uint8_t));

		i16temp = htons(DB_CHUNK_MSG_STORE);
		write_e(db_fd, &i16temp, sizeof(uint16_t));
		write_e(db_fd, &length, sizeof(uint32_t));

		i64temp = htobe64(stored->db_id);
		write_e(db_fd, &i64temp, sizeof(uint64_t));

		i16temp = htons(strlen(stored->source_id));
		write_e(db_fd, &i16temp, sizeof(uint16_t));
		if(i16temp){
			write_e(db_fd, stored->source_id, strlen(stored->source_id));
		}

		i16temp = htons(stored->source_mid);
		write_e(db_fd, &i16temp, sizeof(uint16_t));

		i16temp = htons(stored->msg.mid);
		write_e(db_fd, &i16temp, sizeof(uint16_t));

		i16temp = htons(strlen(stored->msg.topic));
		write_e(db_fd, &i16temp, sizeof(uint16_t));
		write_e(db_fd, stored->msg.topic, strlen(stored->msg.topic));

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

	return 0;
}

static int mqtt3_db_client_write(mosquitto_db *db, int db_fd)
{
	int i;
	uint32_t length;
	uint64_t i64temp;
	uint32_t i32temp;
	uint16_t i16temp;
	uint8_t i8temp;
	mqtt3_context *context;

	assert(db);
	assert(db_fd >= 0);

#define write_e(a, b, c) if(write(a, b, c) != c){ return 1; }

	for(i=0; i<db->context_count; i++){
		context = db->contexts[i];
		if(context){
			if(mqtt3_db_client_messages_write(db, db_fd, context)) return 1;
		}
	}
#undef write_e

	return 0;
}

int mqtt3_db_backup(mosquitto_db *db, bool cleanup)
{
	int rc = 0;
	int db_fd;
	uint32_t db_version = htonl(MQTT_DB_VERSION);
	uint32_t crc = htonl(0);
	uint64_t i64temp;
	uint32_t i32temp;
	uint16_t i16temp;

	if(!db || !db->filepath) return 1;
	mqtt3_log_printf(MOSQ_LOG_INFO, "Saving in-memory database to %s.", db->filepath);
	if(cleanup){
		mqtt3_db_store_clean(db);
	}

	db_fd = open(db->filepath, O_WRONLY|O_CREAT|O_TRUNC, S_IWUSR|S_IRUSR);
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
	i32temp = htons(sizeof(uint16_t)); // FIXME
	write_e(db_fd, &i32temp, sizeof(uint32_t));
	/* last db mid */
	i64temp = htobe64(db->last_db_id);
	write_e(db_fd, &i64temp, sizeof(uint64_t));

	if(mqtt3_db_message_store_write(db, db_fd)){
		goto error;
	}
#undef write_e

	/* FIXME - needs implementing */
	close(db_fd);
	return rc;
error:
	mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	if(db_fd >= 0) close(db_fd);
	return 1;
}

