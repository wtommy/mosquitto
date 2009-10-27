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

void mqtt_handle_publish(int sock, uint8_t header)
{
	uint8_t *topic, *payload;
	uint8_t remaining_length;
	uint8_t dup, qos, retain;

	dup = (header & 0x08)>>3;
	qos = (header & 0x06)>>1;
	retain = (header & 0x01);

	remaining_length = mqtt_read_byte(sock);
	//read(sock, &byte, 1); // Remaining length (should be 12)

	topic = mqtt_read_string(sock);
	remaining_length -= strlen((char *)topic) - 2;
	printf("Topic: %s\n", topic);
	free(topic);

	payload = malloc(remaining_length*sizeof(uint8_t));
	mqtt_read_bytes(sock, payload, remaining_length);
	printf("Payload: %s\n", payload);
	free(payload);
}

void handle_read(int sock)
{
	uint8_t buf;
	read(sock, &buf, 1);

	switch(buf&0xF0){
		case CONNACK:
			printf("Received CONNACK\n");
			read(sock, &buf, 1); // Remaining length
			printf("%d ", buf);
			read(sock, &buf, 1);//Reserved
			printf("%d ", buf);
			read(sock, &buf, 1); // Return code
			printf("%d\n", buf);
			state = stConnAckd;
			break;
		case SUBACK:
			printf("Received SUBACK\n");
			read(sock, &buf, 1); // Remaining length
			printf("%d ", buf);
			read(sock, &buf, 1); // Message ID MSB
			printf("%d ", buf);
			read(sock, &buf, 1); // Message ID LSB
			printf("%d ", buf);
			read(sock, &buf, 1); // Granted QoS
			printf("%d\n", buf);
			state = stSubAckd;
			break;
		case PINGREQ:
			printf("Received PINGREQ\n");
			mqtt_raw_pingresp(sock);
			break;
		case PINGRESP:
			printf("Received PINGRESP\n");
			//FIXME - do something!
			break;
		case PUBLISH:
			printf("Received PUBLISH\n");
			mqtt_handle_publish(sock, buf);
			break;
		default:
			printf("Unknown command: %s (%d)\n", mqtt_command_to_string(buf&0xF0), buf&0xF0);
			break;
	}
}

/* pselect loop test */
int main(int argc, char *argv[])
{
	struct timespec timeout;
	fd_set readfds, writefds;
	int fdcount;
	int run = 1;
	int sock;

	sock = mqtt_connect_socket("127.0.0.1", 1883);
	if(sock == -1){
		return 1;
	}

	state = stSocketOpened;

	while(run){
		FD_ZERO(&readfds);
		FD_SET(sock, &readfds);
		FD_ZERO(&writefds);
		//FD_SET(0, &writefds);
		timeout.tv_sec = 1;
		timeout.tv_nsec = 0;

		fdcount = pselect(sock+1, &readfds, &writefds, NULL, &timeout, NULL);
		if(fdcount == -1){
			fprintf(stderr, "Error in pselect: %s\n", strerror(errno));
			run = 0;
		}else if(fdcount == 0){
			printf("loop timeout\n");
			switch(state){
				case stSocketOpened:
					mqtt_raw_connect(sock, "Roger", 5, false, 0, false, "", 0, "", 0, 10, false);
					state = stConnSent;
					break;
				case stConnSent:
					printf("Waiting for CONNACK\n");
					break;
				case stConnAckd:
					printf("CONNACK received\n");
					mqtt_raw_subscribe(sock, false, "$SYS/broker/log", 15, 0);
					state = stSubSent;
					break;
				case stSubSent:
					printf("Waiting for SUBACK\n");
					break;
				case stSubAckd:
					printf("SUBACK received\n");
					mqtt_raw_publish(sock, false, 0, false, "a/b/c", 5, "Roger", 5);
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

			if(FD_ISSET(sock, &readfds)){
				handle_read(sock);
			}
		}
	}
	return 0;
}

