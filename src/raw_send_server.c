#include <stdint.h>

#include <mqtt3.h>

int mqtt3_raw_connack(mqtt3_context *context, uint8_t result)
{
	if(mqtt3_write_byte(context, CONNACK)) return 1;
	if(mqtt3_write_remaining_length(context, 2)) return 1;
	if(mqtt3_write_byte(context, 0)) return 1;
	if(mqtt3_write_byte(context, result)) return 1;

	return 0;
}

