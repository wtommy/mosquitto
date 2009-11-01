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

int mqtt_handle_connack(mqtt_context *context)
{
	uint32_t remaining_length;
	uint8_t rc;

	printf("Received CONNACK\n");
	remaining_length = mqtt_read_remaining_length(context);
	mqtt_read_byte(context); // Reserved byte, not used
	rc = mqtt_read_byte(context);
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

int mqtt_handle_connect(mqtt_context *context)
{
	uint32_t remaining_length;
	char *protocol_name;
	uint8_t protocol_version;
	uint8_t connect_flags;
	char *client_id;
	char *will_topic, *will_message;
	
	remaining_length = mqtt_read_remaining_length(context);
	protocol_name = mqtt_read_string(context);
	if(!protocol_name){
		return 3;
	}
	if(strcmp(protocol_name, PROTOCOL_NAME)){
		free(protocol_name);
		return 1;
	}
	protocol_version = mqtt_read_byte(context);
	if(protocol_version != PROTOCOL_VERSION){
		free(protocol_name);
		return 1;
	}

	printf("Received CONNECT for protocol %s version %d\n", protocol_name, protocol_version);

	connect_flags = mqtt_read_byte(context);
	context->keepalive = mqtt_read_uint16(context);

	client_id = mqtt_read_string(context);
	if(connect_flags & 0x04){
		will_topic = mqtt_read_string(context);
		will_message = mqtt_read_string(context);
	}

	return 0;
}

int mqtt_handle_puback(mqtt_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	printf("Received PUBACK\n");
	remaining_length = mqtt_read_remaining_length(context);
	mid = mqtt_read_uint16(context);

	if(mid){
		printf("Removing message %d\n", mid);
		mqtt_remove_message(context, mid);
	}
	return 0;
}

int mqtt_handle_pubcomp(mqtt_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	printf("Received PUBCOMP\n");
	remaining_length = mqtt_read_remaining_length(context);
	mid = mqtt_read_uint16(context);

	if(mid){
		printf("Removing message %d\n", mid);
		mqtt_remove_message(context, mid);
	}
	return 0;
}

int mqtt_handle_publish(mqtt_context *context, uint8_t header)
{
	uint8_t *topic, *payload;
	uint32_t remaining_length;
	uint8_t dup, qos, retain;
	uint16_t mid;

	dup = (header & 0x08)>>3;
	qos = (header & 0x06)>>1;
	retain = (header & 0x01);

	printf("dup=%d\nqos=%d\nretain=%d\n", dup, qos, retain);
	remaining_length = mqtt_read_remaining_length(context);

	printf("Remaining length: %d\n", remaining_length);
	topic = mqtt_read_string(context);
	remaining_length -= strlen((char *)topic) + 2;
	printf("Topic: '%s'\n", topic);
	free(topic);

	if(qos > 0){
		mid = mqtt_read_uint16(context);
	}

	printf("Remaining length: %d\n", remaining_length);
	payload = calloc((remaining_length+1), sizeof(uint8_t));
	mqtt_read_bytes(context, payload, remaining_length);
	printf("Payload: '%s'\n", payload);
	free(payload);

	if(qos == 1){
		mqtt_raw_puback(context, mid);
	}

	return 0;
}

int mqtt_handle_pubrec(mqtt_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	/* FIXME - deal with mid properly */
	printf("Received PUBREC\n");
	remaining_length = mqtt_read_remaining_length(context);
	mid = mqtt_read_uint16(context);

	mqtt_raw_pubrel(context, mid);

	return 0;
}

int mqtt_handle_suback(mqtt_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;
	uint8_t granted_qos;

	printf("Received SUBACK\n");
	remaining_length = mqtt_read_remaining_length(context);

	mid = mqtt_read_uint16(context);
	remaining_length -= 2;

	while(remaining_length){
		/* FIXME - Need to do something with this */
		granted_qos = mqtt_read_byte(context);
		printf("Granted QoS %d\n", granted_qos);
		remaining_length--;
	}

	return 0;
}

int mqtt_handle_unsuback(mqtt_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	printf("Received UNSUBACK\n");
	remaining_length = mqtt_read_remaining_length(context);
	mid = mqtt_read_uint16(context);

	return 0;
}
