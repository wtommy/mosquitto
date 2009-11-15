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

/* FIXME - Incomplete */
int mqtt3_handle_connect(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint8_t *protocol_name;
	uint8_t protocol_version;
	uint8_t connect_flags;
	uint8_t *client_id;
	uint8_t *will_topic = NULL, *will_message = NULL;
	uint8_t will, will_retain, will_qos, clean_start;
	int oldsock;
	
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_string(context, &protocol_name)) return 1;
	if(!protocol_name){
		mqtt3_context_cleanup(context);
		return 3;
	}
	if(strcmp(protocol_name, PROTOCOL_NAME)){
		free(protocol_name);
		mqtt3_context_cleanup(context);
		return 1;
	}
	if(mqtt3_read_byte(context, &protocol_version)) return 1;
	if(protocol_version != PROTOCOL_VERSION){
		free(protocol_name);
		// FIXME - should disconnect as well
		mqtt3_raw_connack(context, 1);
		mqtt3_context_cleanup(context);
		return 1;
	}

	printf("Received CONNECT for protocol %s version %d\n", protocol_name, protocol_version);
	free(protocol_name);

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
		free(context->id);
	}

	context->id = client_id;

	if(!mqtt3_db_client_find_socket(client_id, &oldsock)){
		if(oldsock == -1){
			/* Client is reconnecting after a disconnect */
		}else{
			/* Client is already connected, disconnect old version */
			fprintf(stderr, "Client %s already connected, closing old connection.\n", client_id);
			close(oldsock);
		}
		mqtt3_db_client_update(context, will, will_retain, will_qos, will_topic, will_message);
	}else{
		/* FIXME - act on return value */
		mqtt3_db_client_insert(context, will, will_retain, will_qos, will_topic, will_message);
	}

	if(clean_start){
		mqtt3_db_subs_clean_start(context);
	}
	/* FIXME - save will */

	if(will_topic) free(will_topic);
	if(will_message) free(will_message);

	return mqtt3_raw_connack(context, 0);
}

int mqtt3_handle_disconnect(mqtt3_context *context)
{
	uint32_t remaining_length;

	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	return mqtt3_socket_close(context);
}


int mqtt3_handle_subscribe(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;
	uint8_t *sub;
	uint8_t qos;
	uint8_t *payload = NULL;
	uint8_t payloadlen = 0;

	if(!context) return 1;

	printf("Received SUBSCRIBE\n");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;
	remaining_length -= 2;

	while(remaining_length){
		sub = NULL;
		if(mqtt3_read_string(context, &sub)){
			if(sub) free(sub);
			return 1;
		}

		remaining_length -= strlen(sub) + 2;
		if(mqtt3_read_byte(context, &qos)) return 1;
		remaining_length -= 1;
		if(sub){
			mqtt3_db_sub_insert(context, sub, qos);
			free(sub);
		}

		payload = realloc(payload, payloadlen + 1);
		payload[payloadlen] = qos;
		payloadlen++;
	}

	if(mqtt3_write_byte(context, SUBACK)) return 1;
	if(mqtt3_write_remaining_length(context, payloadlen+2)) return 1;
	if(mqtt3_write_uint16(context, mid)) return 1;
	if(mqtt3_write_bytes(context, payload, payloadlen)) return 1;

	free(payload);
	
	return 0;
}

int mqtt3_handle_unsubscribe(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;
	uint8_t *sub;

	if(!context) return 1;

	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;
	remaining_length -= 2;

	while(remaining_length){
		sub = NULL;
		if(mqtt3_read_string(context, &sub)){
			if(sub) free(sub);
			return 1;
		}

		remaining_length -= strlen(sub) + 2;
		if(sub){
			mqtt3_db_sub_delete(context, sub);
			free(sub);
		}
	}

	if(mqtt3_write_byte(context, UNSUBACK)) return 1;
	if(mqtt3_write_remaining_length(context, 2)) return 1;
	if(mqtt3_write_uint16(context, mid)) return 1;

	return 0;
}

