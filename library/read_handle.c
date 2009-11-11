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

int mqtt3_handle_connack(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint8_t byte;
	uint8_t rc;

	printf("Received CONNACK\n");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_byte(context, &byte)) return 1; // Reserved byte, not used
	if(mqtt3_read_byte(context, &rc)) return 1;
	switch(rc){
		case 0:
			return 0;
		case 1:
			fprintf(stderr, "Connection Refused: unacceptable protocol version\n");
			return 1;
		case 2:
			fprintf(stderr, "Connection Refused: identifier rejected\n");
			return 1;
		case 3:
			fprintf(stderr, "Connection Refused: broker unavailable\n");
			return 1;
	}
	return 1;
}

int mqtt3_handle_connect(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint8_t *protocol_name;
	uint8_t protocol_version;
	uint8_t connect_flags;
	uint8_t *client_id;
	uint8_t *will_topic = NULL, *will_message = NULL;
	uint8_t will, will_retain, will_qos, clean_start;
	
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_string(context, &protocol_name)) return 1;
	if(!protocol_name){
		return 3;
	}
	if(strcmp(protocol_name, PROTOCOL_NAME)){
		free(protocol_name);
		return 1;
	}
	if(mqtt3_read_byte(context, &protocol_version)) return 1;
	if(protocol_version != PROTOCOL_VERSION){
		free(protocol_name);
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
	/* FIXME - save will */

	/* FIXME - act on return value */
	mqtt3_db_insert_client(context, will, will_retain, will_qos, will_topic, will_message);

	if(will_topic) free(will_topic);
	if(will_message) free(will_message);

	return mqtt3_raw_connack(context, 0);
}

int mqtt3_handle_disconnect(mqtt3_context *context)
{
	uint32_t remaining_length;

	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;

	/* FIXME - handle this correctly */
	return 0;
}

int mqtt3_handle_puback(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	printf("Received PUBACK\n");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;

	if(mid){
		printf("Removing message %d\n", mid);
		mqtt3_remove_message(context, mid);
	}
	return 0;
}

int mqtt3_handle_pingreq(mqtt3_context *context)
{
	uint32_t remaining_length;

	printf("Received PINGREQ\n");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;

	return mqtt3_raw_pingresp(context);
}

int mqtt3_handle_pubcomp(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	printf("Received PUBCOMP\n");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;

	if(mid){
		printf("Removing message %d\n", mid);
		mqtt3_remove_message(context, mid);
	}
	return 0;
}

int mqtt3_handle_publish(mqtt3_context *context, uint8_t header)
{
	uint8_t *topic, *payload;
	uint32_t remaining_length;
	uint8_t dup, qos, retain;
	uint16_t mid;

	dup = (header & 0x08)>>3;
	qos = (header & 0x06)>>1;
	retain = (header & 0x01);

	printf("dup=%d\nqos=%d\nretain=%d\n", dup, qos, retain);
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;

	printf("Remaining length: %d\n", remaining_length);
	if(mqtt3_read_string(context, &topic)) return 1;
	remaining_length -= strlen((char *)topic) + 2;
	printf("Topic: '%s'\n", topic);
	free(topic);

	if(qos > 0){
		if(mqtt3_read_uint16(context, &mid)) return 1;
		remaining_length -= 2;
	}

	printf("Remaining length: %d\n", remaining_length);
	payload = calloc((remaining_length+1), sizeof(uint8_t));
	if(mqtt3_read_bytes(context, payload, remaining_length)) return 1;
	printf("Payload: '%s'\n", payload);
	free(payload);

	if(qos == 1){
		mqtt3_raw_puback(context, mid);
	}

	return 0;
}

int mqtt3_handle_pubrec(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	/* FIXME - deal with mid properly */
	printf("Received PUBREC\n");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;

	mqtt3_raw_pubrel(context, mid);

	return 0;
}

int mqtt3_handle_suback(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;
	uint8_t granted_qos;

	printf("Received SUBACK\n");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;

	if(mqtt3_read_uint16(context, &mid)) return 1;
	remaining_length -= 2;

	while(remaining_length){
		/* FIXME - Need to do something with this */
		if(mqtt3_read_byte(context, &granted_qos)) return 1;
		printf("Granted QoS %d\n", granted_qos);
		remaining_length--;
	}

	return 0;
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
		/* FIXME - Need to do something with this */
		sub = NULL;
		if(mqtt3_read_string(context, &sub)){
			if(sub) free(sub);
			return 1;
		}

		remaining_length -= strlen(sub) + 2;
		if(mqtt3_read_byte(context, &qos)) return 1;
		remaining_length -= 1;
		if(sub){
			mqtt3_db_insert_sub(context, sub, qos);
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

int mqtt3_handle_unsuback(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	printf("Received UNSUBACK\n");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;

	return 0;
}

int mqtt3_handle_unsubscribe(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;
	uint8_t *sub;

	if(!context) return 1;

	printf("Received UNSUBSCRIBE\n");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;
	remaining_length -= 2;

	while(remaining_length){
		/* FIXME - Need to do something with this */
		sub = NULL;
		if(mqtt3_read_string(context, &sub)){
			if(sub) free(sub);
			return 1;
		}

		remaining_length -= strlen(sub) + 2;
		if(sub){
			mqtt3_db_delete_sub(context, sub);
			free(sub);
		}
	}

	if(mqtt3_write_byte(context, UNSUBACK)) return 1;
	if(mqtt3_write_remaining_length(context, 2)) return 1;
	if(mqtt3_write_uint16(context, mid)) return 1;

	return 0;
}

