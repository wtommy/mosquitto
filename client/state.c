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

typedef enum {
	stStart,
	stSocketOpened,
	stConnSent,
	stConnAckd,
	stSubSent,
	stSubAckd,
	stPause
} stateType;

static stateType state = stStart;

void mqtt_check_keepalive(mqtt_context *context)
{
	if(time(NULL) - context->last_message >= context->keepalive){
		mqtt_raw_pingreq(context);
	}
}

void mqtt_handle_publish(mqtt_context *context, uint8_t header)
{
	uint8_t *topic, *payload;
	uint32_t remaining_length;
	uint8_t dup, qos, retain;

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

	printf("Remaining length: %d\n", remaining_length);
	payload = calloc((remaining_length+1), sizeof(uint8_t));
	mqtt_read_bytes(context, payload, remaining_length);
	printf("Payload: '%s'\n", payload);
	free(payload);
}

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

int mqtt_handle_puback(mqtt_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	/* FIXME - deal with mid and check that there are no more remaining bytes */
	printf("Received PUBACK\n");
	remaining_length = mqtt_read_remaining_length(context);
	mid = mqtt_read_uint16(context);

	return 0;
}

int mqtt_handle_pubcomp(mqtt_context *context)
{
	uint32_t remaining_length;
	uint16_t mid;

	/* FIXME - deal with mid and check that there are no more remaining bytes */
	printf("Received PUBCOMP\n");
	remaining_length = mqtt_read_remaining_length(context);
	mid = mqtt_read_uint16(context);

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

int handle_read(mqtt_context *context)
{
	uint8_t buf;
	int rc;

	rc = read(context->sock, &buf, 1);
	printf("rc: %d\n", rc);
	if(rc == -1){
		printf("Error: %s\n", strerror(errno));
		return 1;
	}else if(rc == 0){
		return 2;
	}

	switch(buf&0xF0){
		case CONNACK:
			if(mqtt_handle_connack(context)){
				return 3;
			}
			state = stConnAckd;
			break;
		case SUBACK:
			mqtt_handle_suback(context);
			state = stSubAckd;
			break;
		case PINGREQ:
			printf("Received PINGREQ\n");
			mqtt_read_remaining_length(context);
			mqtt_raw_pingresp(context);
			break;
		case PINGRESP:
			printf("Received PINGRESP\n");
			mqtt_read_remaining_length(context);
			//FIXME - do something!
			break;
		case PUBACK:
			mqtt_handle_puback(context);
			break;
		case PUBCOMP:
			mqtt_handle_pubcomp(context);
			break;
		case PUBLISH:
			printf("Received PUBLISH\n");
			mqtt_handle_publish(context, buf);
			break;
		case PUBREC:
			mqtt_handle_pubrec(context);
			break;
		case UNSUBACK:
			mqtt_handle_unsuback(context);
			break;
		default:
			printf("Unknown command: %s (%d)\n", mqtt_command_to_string(buf&0xF0), buf&0xF0);
			break;
	}
	return 0;
}

/* pselect loop test */
int main(int argc, char *argv[])
{
	struct timespec timeout;
	fd_set readfds, writefds;
	int fdcount;
	int run = 1;
	mqtt_context context;

	context.sock = mqtt_connect_socket("127.0.0.1", 1883);
	if(context.sock == -1){
		return 1;
	}

	state = stSocketOpened;

	while(run){
		FD_ZERO(&readfds);
		FD_SET(context.sock, &readfds);
		FD_ZERO(&writefds);
		//FD_SET(0, &writefds);
		timeout.tv_sec = 1;
		timeout.tv_nsec = 0;

		fdcount = pselect(context.sock+1, &readfds, &writefds, NULL, &timeout, NULL);
		if(fdcount == -1){
			fprintf(stderr, "Error in pselect: %s\n", strerror(errno));
			run = 0;
		}else if(fdcount == 0){
			printf("loop timeout\n");
			switch(state){
				case stSocketOpened:
					mqtt_raw_connect(&context, "Roger", 5, false, 0, false, "", 0, "", 0, 10, false);
					state = stConnSent;
					break;
				case stConnSent:
					printf("Waiting for CONNACK\n");
					break;
				case stConnAckd:
					printf("CONNACK received\n");
					mqtt_raw_subscribe(&context, false, "$SYS/#", 6, 0);
					state = stSubSent;
					break;
				case stSubSent:
					printf("Waiting for SUBACK\n");
					break;
				case stSubAckd:
					printf("SUBACK received\n");
					mqtt_raw_unsubscribe(&context, false, "$SYS/#", 6);
					mqtt_raw_publish(&context, false, 2, false, "a/b/c", 5, (uint8_t *)"Roger", 5);
					state = stPause;
					break;
				case stPause:
					printf("Pause\n");
					break;
				default:
					fprintf(stderr, "Error: Unknown state\n");
					break;
			}
		}else{
			printf("fdcount=%d\n", fdcount);

			if(FD_ISSET(context.sock, &readfds)){
				if(handle_read(&context)){
					fprintf(stderr, "Socket closed on remote side\n");
					close(context.sock);
					run = 0;
				}
			}
		}
		mqtt_check_keepalive(&context);
	}
	return 0;
}

