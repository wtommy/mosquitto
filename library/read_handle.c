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
	uint8_t *will_topic, *will_message;
	
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
	if(mqtt3_read_uint16(context, &(context->keepalive))) return 1;

	if(mqtt3_read_string(context, &client_id)) return 1;
	free(client_id);
	if(connect_flags & 0x04){
		if(mqtt3_read_string(context, &will_topic)) return 1;
		free(will_topic);
		if(mqtt3_read_string(context, &will_message)) return 1;
		free(will_message);
	}

	return mqtt3_raw_connack(context, 0);
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

int mqtt3_handle_unsuback(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	printf("Received UNSUBACK\n");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;

	return 0;
}
