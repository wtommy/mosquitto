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

int mqtt3_handle_puback(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	printf("Received PUBACK\n");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;

	if(mid){
		printf("Removing message %d\n", mid);
		mqtt3_message_remove(context, mid);
	}
	return 0;
}

int mqtt3_handle_pingreq(mqtt3_context *context)
{
	uint32_t remaining_length;
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	return mqtt3_raw_pingresp(context);
}

int mqtt3_handle_pingresp(mqtt3_context *context)
{
	uint32_t remaining_length;
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
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
		mqtt3_message_remove(context, mid);
	}
	return 0;
}

int mqtt3_handle_publish(mqtt3_context *context, uint8_t header)
{
	char *sub;
	uint8_t *payload;
	uint32_t remaining_length;
	uint32_t payloadlen;
	uint8_t dup, qos, retain;
	uint16_t mid;
	int rc = 0;

	dup = (header & 0x08)>>3;
	qos = (header & 0x06)>>1;
	retain = (header & 0x01);

	printf("dup=%d\nqos=%d\nretain=%d\n", dup, qos, retain);
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;

	printf("Remaining length: %d\n", remaining_length);
	if(mqtt3_read_string(context, &sub)) return 1;
	remaining_length -= strlen((char *)sub) + 2;
	printf("Topic: '%s'\n", sub);

	if(qos > 0){
		if(mqtt3_read_uint16(context, &mid)){
			mqtt3_free(sub);
			return 1;
		}
		remaining_length -= 2;
	}

	printf("Remaining length: %d\n", remaining_length);
	payloadlen = remaining_length;
	payload = mqtt3_calloc(payloadlen, sizeof(uint8_t));
	if(mqtt3_read_bytes(context, payload, payloadlen)){
		mqtt3_free(sub);
		return 1;
	}
	printf("Payload: '%s'\n", payload);

	switch(qos){
		case 0:
			if(mqtt3_db_messages_queue(sub, qos, payloadlen, payload, retain)) rc = 1;
			break;
		case 1:
			/* FIXME - need to store message somewhere */
			if(mqtt3_raw_puback(context, mid)) rc = 1;
			break;
		case 2:
			/* FIXME - need to store message somewhere */
			if(mqtt3_raw_pubrec(context, mid)) rc = 1;
			break;
	}
	mqtt3_free(sub);
	mqtt3_free(payload);

	return rc;
}

int mqtt3_handle_pubrec(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	/* FIXME - deal with mid properly */
	printf("Received PUBREC\n");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;

	if(mqtt3_raw_pubrel(context, mid)) return 1;

	return 0;
}

int mqtt3_handle_pubrel(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	/* FIXME - deal with mid properly */
	printf("Received PUBREL\n");
	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;

	if(mqtt3_raw_pubcomp(context, mid)) return 1;

	return 0;
}

