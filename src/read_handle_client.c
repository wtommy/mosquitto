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

int mqtt3_handle_suback(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;
	uint8_t granted_qos;

	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;

	if(mqtt3_read_uint16(context, &mid)) return 1;
	remaining_length -= 2;

	while(remaining_length){
		/* FIXME - Need to do something with this */
		if(mqtt3_read_byte(context, &granted_qos)) return 1;
		remaining_length--;
	}

	return 0;
}

int mqtt3_handle_unsuback(mqtt3_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	if(mqtt3_read_remaining_length(context, &remaining_length)) return 1;
	if(mqtt3_read_uint16(context, &mid)) return 1;

	return 0;
}
