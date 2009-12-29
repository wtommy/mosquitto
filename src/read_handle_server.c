/*
Copyright (c) 2009, Roger Light <roger@atchoo.org>
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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>

#include <mqtt3.h>

int mqtt3_handle_connect(mqtt3_context *context)
{
	uint32_t remaining_length;
	char *protocol_name;
	uint8_t protocol_version;
	uint8_t connect_flags;
	char *client_id;
	char *will_topic = NULL, *will_message = NULL;
	uint8_t will, will_retain, will_qos, clean_start;
	
	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received CONNECT");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_string(context, &protocol_name)) return 1;
	if(!protocol_name){
		mqtt3_context_cleanup(context);
		return 3;
	}
	if(strcmp(protocol_name, PROTOCOL_NAME)){
		mqtt3_free(protocol_name);
		mqtt3_context_cleanup(context);
		return 1;
	}
	if(mqtt3_read_byte(context, &protocol_version)) return 1;
	if(protocol_version != PROTOCOL_VERSION){
		mqtt3_free(protocol_name);
		mqtt3_raw_connack(context, 1);
		mqtt3_context_cleanup(context);
		return 1;
	}

	mqtt3_free(protocol_name);

	if(mqtt3_read_byte(context, &connect_flags)) return 1;
	clean_start = connect_flags & 0x02;
	will = connect_flags & 0x04;
	will_qos = (connect_flags & 0x18) >> 2;
	will_retain = connect_flags & 0x20;

	if(mqtt3_read_uint16(context, &(context->keepalive))) return 1;

	if(mqtt3_read_string(context, &client_id)) return 1;
	if(connect_flags & 0x04){
		if(mqtt3_read_string(context, &will_topic)) return 1;
		if(mqtt3_read_string(context, &will_message)) return 1;
	}

	if(context->id){
		/* FIXME - second CONNECT!
		 * FIXME - Need to check for existing client with same name
		 * FIXME - Need to check for valid name
		 */
		mqtt3_free(context->id);
	}

	context->id = client_id;
	context->clean_start = clean_start;

	mqtt3_db_client_insert(context, will, will_retain, will_qos, will_topic, will_message);

	if(will_topic) mqtt3_free(will_topic);
	if(will_message) mqtt3_free(will_message);

	return mqtt3_raw_connack(context, 0);
}

int mqtt3_handle_disconnect(mqtt3_context *context)
{
	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received DISCONNECT");
	return mqtt3_socket_close(context);
}


int mqtt3_handle_subscribe(mqtt3_context *context)
{
	int rc = 0;
	uint32_t remaining_length;
	uint16_t mid;
	char *sub;
	uint8_t qos;
	uint8_t *payload = NULL;
	uint32_t payloadlen = 0;

	uint16_t retain_mid;
	int retain_qos;
	uint8_t *retain_payload = NULL;
	uint32_t retain_payloadlen;

	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received SUBSCRIBE");
	/* FIXME - plenty of potential for memory leaks here */
	if(!context) return 1;

	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;
	remaining_length -= 2;

	while(remaining_length){
		sub = NULL;
		if(mqtt3_read_string(context, &sub)){
			if(sub) mqtt3_free(sub);
			return 1;
		}

		remaining_length -= strlen(sub) + 2;
		if(mqtt3_read_byte(context, &qos)) return 1;
		remaining_length -= 1;
		if(sub){
			mqtt3_db_sub_insert(context->id, sub, qos);
	
			if(!mqtt3_db_retain_find(sub, &retain_qos, &retain_payloadlen, &retain_payload)){
				if(retain_qos > qos) retain_qos = qos;
				if(retain_qos > 0){
					retain_mid = mqtt3_db_mid_generate(context->id);
				}else{
					retain_mid = 0;
				}
				switch(retain_qos){
					case 0:
						if(mqtt3_db_message_insert(context->id, retain_mid, md_out, ms_publish, 1,
								sub, retain_qos, retain_payloadlen, retain_payload)) rc = 1;
						break;
					case 1:
						if(mqtt3_db_message_insert(context->id, retain_mid, md_out, ms_publish_puback, 1,
								sub, retain_qos, retain_payloadlen, retain_payload)) rc = 1;
						break;
					case 2:
						if(mqtt3_db_message_insert(context->id, retain_mid, md_out, ms_publish_pubrec, 1,
								sub, retain_qos, retain_payloadlen, retain_payload)) rc = 1;
						break;
				}
				if(retain_payload){
					mqtt3_free(retain_payload);
				}
			}
			mqtt3_free(sub);
		}

		payload = mqtt3_realloc(payload, payloadlen + 1);
		payload[payloadlen] = qos;
		payloadlen++;
	}

	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Sending SUBACK to %d", context->sock);
	if(mqtt3_write_byte(context, SUBACK)) return 1;
	if(mqtt3_write_remaining_length(context, payloadlen+2)) return 1;
	if(mqtt3_write_uint16(context, mid)) return 1;
	if(mqtt3_write_bytes(context, payload, payloadlen)) return 1;

	mqtt3_free(payload);
	
	return rc;
}

int mqtt3_handle_unsubscribe(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;
	char *sub;

	mqtt3_log_printf(MQTT3_LOG_DEBUG, "Received UNSUBSCRIBE");
	if(!context) return 1;

	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;
	remaining_length -= 2;

	while(remaining_length){
		sub = NULL;
		if(mqtt3_read_string(context, &sub)){
			if(sub) mqtt3_free(sub);
			return 1;
		}

		remaining_length -= strlen(sub) + 2;
		if(sub){
			mqtt3_db_sub_delete(context->id, sub);
			mqtt3_free(sub);
		}
	}

	if(mqtt3_write_byte(context, UNSUBACK)) return 1;
	if(mqtt3_write_remaining_length(context, 2)) return 1;
	if(mqtt3_write_uint16(context, mid)) return 1;

	return 0;
}

