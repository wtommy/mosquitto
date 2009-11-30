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

	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;

	if(mid){
		if(mqtt3_db_message_delete(context->id, mid, md_out)) return 1;
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

	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;

	if(mid){
		if(mqtt3_db_message_delete(context->id, mid, md_out)) return 1;
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

	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;

	if(mqtt3_read_string(context, &sub)) return 1;
	remaining_length -= strlen((char *)sub) + 2;

	if(qos > 0){
		if(mqtt3_read_uint16(context, &mid)){
			mqtt3_free(sub);
			return 1;
		}
		remaining_length -= 2;
	}

	payloadlen = remaining_length;
	payload = mqtt3_calloc(payloadlen, sizeof(uint8_t));
	if(mqtt3_read_bytes(context, payload, payloadlen)){
		mqtt3_free(sub);
		return 1;
	}

	switch(qos){
		case 0:
			if(mqtt3_db_messages_queue(sub, qos, payloadlen, payload, retain)) rc = 1;
		case 1:
			if(mqtt3_raw_puback(context, mid)) rc = 1;
			break;
		case 2:
			if(mqtt3_db_message_insert(context->id, mid, md_in, ms_wait_pubrec, retain, sub, qos, payloadlen, payload)) rc = 1;
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

	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;

	if(mqtt3_db_message_update(context->id, mid, md_out, ms_wait_pubcomp)) return 1;
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

