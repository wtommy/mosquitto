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

void mqtt3_check_keepalive(mqtt3_context *context)
{
	if(time(NULL) - context->last_msg_out >= context->keepalive){
		mqtt3_raw_pingreq(context);
	}
}

int handle_read(mqtt3_context *context)
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
			if(mqtt3_handle_connack(context)) return 3;
			state = stConnAckd;
			break;
		case SUBACK:
			if(mqtt3_handle_suback(context)) return 3;
			state = stSubAckd;
			break;
		case PINGREQ:
			if(mqtt3_handle_pingreq(context)) return 3;
			break;
		case PINGRESP:
			if(mqtt3_handle_pingresp(context)) return 3;
			break;
		case PUBACK:
			if(mqtt3_handle_puback(context)) return 3;
			break;
		case PUBCOMP:
			if(mqtt3_handle_pubcomp(context)) return 3;
			break;
		case PUBLISH:
			if(mqtt3_handle_publish(context, buf)) return 3;
			break;
		case PUBREC:
			if(mqtt3_handle_pubrec(context)) return 3;
			break;
		case UNSUBACK:
			if(mqtt3_handle_unsuback(context)) return 3;
			break;
		default:
			printf("Unknown command: %s (%d)\n", mqtt3_command_to_string(buf&0xF0), buf&0xF0);
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
	mqtt3_context context;
	mqtt3_message *pointer;

	context.sock = mqtt3_connect_socket("127.0.0.1", 1883);
	if(context.sock == -1){
		return 1;
	}
	context.messages = NULL;

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
			pointer = context.messages;
			while(pointer){
				printf("Message: %s\n", mqtt3_command_to_string(pointer->command));
				pointer = pointer->next;
			}
			switch(state){
				case stSocketOpened:
					mqtt3_raw_connect(&context, "Roger", 5, false, 0, false, "", 0, "", 0, 10, false);
					state = stConnSent;
					break;
				case stConnSent:
					printf("Waiting for CONNACK\n");
					break;
				case stConnAckd:
					printf("CONNACK received\n");
					mqtt3_raw_subscribe(&context, false, "a/b/c", 5, 0);
					state = stSubSent;
					break;
				case stSubSent:
					printf("Waiting for SUBACK\n");
					break;
				case stSubAckd:
					printf("SUBACK received\n");
					mqtt3_managed_publish(&context, 2, false, "a/b/c", 5, (uint8_t *)"Roger", 5);
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
			pointer = context.messages;
			while(pointer){
				printf("Message: %s\n", mqtt3_command_to_string(pointer->command));
				pointer = pointer->next;
			}

			if(FD_ISSET(context.sock, &readfds)){
				if(handle_read(&context)){
					fprintf(stderr, "Socket closed on remote side\n");
					mqtt3_close_socket(&context);
					run = 0;
				}
			}
		}
		mqtt3_check_keepalive(&context);
	}
	return 0;
}

