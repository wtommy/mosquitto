#include <stdint.h>

#include <mqtt3.h>

int mqtt3_raw_puback(mqtt3_context *context, uint16_t mid)
{
	return mqtt3_send_command_with_mid(context, PUBACK, mid);
}

int mqtt3_raw_publish(mqtt3_context *context, bool dup, uint8_t qos, bool retain, const char *topic, uint16_t topiclen, const uint8_t *payload, uint32_t payloadlen)
{
	int packetlen;
	uint16_t mid;

	packetlen = 2+topiclen + payloadlen;
	if(qos > 0) packetlen += 2; /* For message id */

	/* Fixed header */
	if(mqtt3_write_byte(context, PUBLISH | (dup<<3) | (qos<<1) | retain)) return 1;
	if(mqtt3_write_remaining_length(context, packetlen)) return 1;

	/* Variable header (topic string) */
	if(mqtt3_write_string(context, topic, topiclen)) return 1;
	if(qos > 0){
		mid = mqtt3_db_mid_generate(context->id);
		if(mqtt3_write_uint16(context, mid)) return 1;
	}

	/* Payload */
	if(mqtt3_write_bytes(context, payload, payloadlen)) return 1;

	context->last_msg_out = time(NULL);
	return 0;
}

int mqtt3_raw_pubcomp(mqtt3_context *context, uint16_t mid)
{
	return mqtt3_send_command_with_mid(context, PUBCOMP, mid);
}

int mqtt3_raw_pubrec(mqtt3_context *context, uint16_t mid)
{
	return mqtt3_send_command_with_mid(context, PUBREC, mid);
}

int mqtt3_raw_pubrel(mqtt3_context *context, uint16_t mid)
{
	return mqtt3_send_command_with_mid(context, PUBREL, mid);
}

/* For PUBACK, PUBCOMP, PUBREC, and PUBREL */
int mqtt3_send_command_with_mid(mqtt3_context *context, uint8_t command, uint16_t mid)
{
	if(mqtt3_write_byte(context, command)) return 1;
	if(mqtt3_write_remaining_length(context, 2)) return 1;
	if(mqtt3_write_uint16(context, mid)) return 1;

	context->last_msg_out = time(NULL);
	return 0;
}

/* For DISCONNECT, PINGREQ and PINGRESP */
int mqtt3_send_simple_command(mqtt3_context *context, uint8_t command)
{
	if(mqtt3_write_byte(context, command)) return 1;
	if(mqtt3_write_byte(context, 0)) return 1;

	context->last_msg_out = time(NULL);
	return 0;
}

int mqtt3_raw_pingreq(mqtt3_context *context)
{
	return mqtt3_send_simple_command(context, PINGREQ);
}

int mqtt3_raw_pingresp(mqtt3_context *context)
{
	return mqtt3_send_simple_command(context, PINGRESP);
}

