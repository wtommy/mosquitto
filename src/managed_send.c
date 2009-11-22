#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <mqtt3.h>

int mqtt3_managed_publish(mqtt3_context *context, uint8_t qos, bool retain, const char *topic, uint16_t topiclen, const uint8_t *payload, uint32_t payloadlen)
{
	int packetlen;
	uint16_t mid;
	mqtt3_message *message;

	if(qos == 0){
		mqtt3_raw_publish(context, 0, qos, retain, topic, topiclen, payload, payloadlen);
	}

	packetlen = 2+topiclen + payloadlen;
	if(qos > 0) packetlen += 2; /* For message id */

	message = mqtt3_calloc(sizeof(mqtt3_message), 1);
	
	message->command = PUBLISH;
	message->dup = 0;
	message->qos = qos;
	message->retain = retain;
	message->remaining_length = packetlen;
	if(qos > 0){
		mid = mqtt3_generate_message_id();
	}else{
		mid = 0;
	}
	message->message_id = mid;
	if(qos > 0){
		message->variable_header = mqtt3_malloc((2+2+topiclen)*sizeof(uint8_t));
		message->variable_header[2+topiclen] = MQTT_MSB(mid);
		message->variable_header[2+topiclen+1] = MQTT_LSB(mid);
		message->variable_header_len = 2+2+topiclen;
	}else{
		message->variable_header = mqtt3_malloc((2+topiclen)*sizeof(uint8_t));
		message->variable_header_len = 2+topiclen;
	}
	message->variable_header[0] = MQTT_MSB(topiclen);
	message->variable_header[1] = MQTT_LSB(topiclen);
	memcpy(&(message->variable_header[2]), topic, topiclen);
	message->payload = mqtt3_malloc(payloadlen*sizeof(uint8_t));
	memcpy(message->payload, payload, payloadlen);
	message->payload_len = payloadlen;

	if(message->qos){
		mqtt3_message_add(context, message);
	}
	mqtt3_managed_send(context, message);
	return 0;
}

int mqtt3_managed_send(mqtt3_context *context, mqtt3_message *message)
{
	if(!context || !message) return 1;

	if(mqtt3_write_byte(context, message->command | (message->dup<<3) | (message->qos<<1) | message->retain)) return 1;
	if(mqtt3_write_remaining_length(context, message->remaining_length)) return 1;

	if(mqtt3_write_bytes(context, message->variable_header, message->variable_header_len)) return 1;
	if(mqtt3_write_bytes(context, message->payload, message->payload_len)) return 1;
	
	if(message->qos){
		message->timestamp = time(NULL);
	}else{
		mqtt3_message_cleanup(message);
	}

	context->last_msg_out = time(NULL);

	return 0;
}
