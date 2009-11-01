#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mqtt3.h>

int mqtt_listen_socket(uint16_t port)
{
	int sock;
	struct sockaddr_in addr;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == -1){
		fprintf(stderr, "Error: %s\n", strerror(errno));
		return -1;
	}

	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if(bind(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) == -1){
		fprintf(stderr, "Error: %s\n", strerror(errno));
		fflush(stderr);
		return -1;
	}

	if(listen(sock, 100) == -1){
		fprintf(stderr, "Error: %s\n", strerror(errno));
		fflush(stderr);
		return -1;
	}

	return sock;
}

mqtt_context *mqtt_init_context(int sock)
{
	mqtt_context *context;

	context = malloc(sizeof(mqtt_context));
	if(!context) return NULL;
	
	context->next = NULL;
	context->sock = sock;
	context->last_message = time(NULL);
	context->keepalive = 60; /* Default to 60s */
	context->messages = NULL;
	context->subscriptions = NULL;

	return context;
}

int handle_read(mqtt_context *context)
{
	uint8_t byte;

	byte = mqtt_read_byte(context);
	switch(byte&0xF0){
		case CONNECT:
			mqtt_handle_connect(context);
			break;
		default:
			printf("Received command: %s (%d)\n", mqtt_command_to_string(byte&0xF0), byte&0xF0);
			break;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct timespec timeout;
	fd_set readfds, writefds;
	int fdcount;
	int listensock;
	int new_sock;
	mqtt_context *contexts = NULL;
	mqtt_context *ctxt_ptr;
	mqtt_context *new_context;
	int sockmax;

	if(mqtt_db_open("mqtt_broker.db")){
		fprintf(stderr, "Error: Couldn't open database.\n");
	}

	listensock = mqtt_listen_socket(1883);
	if(listensock == -1){
		return 1;
	}

	while(1){
		FD_ZERO(&readfds);
		FD_SET(listensock, &readfds);

		sockmax = listensock;
		ctxt_ptr = contexts;
		while(ctxt_ptr){
			if(ctxt_ptr->sock != -1){
				FD_SET(ctxt_ptr->sock, &readfds);
				if(ctxt_ptr->sock > sockmax){
					sockmax = ctxt_ptr->sock;
				}
			}
			ctxt_ptr = ctxt_ptr->next;
		}

		FD_ZERO(&writefds);
		timeout.tv_sec = 1;
		timeout.tv_nsec = 0;

		fdcount = pselect(sockmax+1, &readfds, &writefds, NULL, &timeout, NULL);
		if(fdcount == -1){
			fprintf(stderr, "Error in pselect: %s\n", strerror(errno));
		}else if(fdcount == 0){
			printf("loop timeout\n");
		}else{
			ctxt_ptr = contexts;
			while(ctxt_ptr){
				if(ctxt_ptr->sock != -1 && FD_ISSET(ctxt_ptr->sock, &readfds)){
					handle_read(ctxt_ptr);
				}
				ctxt_ptr = ctxt_ptr->next;
			}
			if(FD_ISSET(listensock, &readfds)){
				new_sock = accept(listensock, NULL, 0);
				new_context = mqtt_init_context(new_sock);
				if(contexts){
					ctxt_ptr = contexts;
					while(ctxt_ptr->next){
						ctxt_ptr = ctxt_ptr->next;
					}
					ctxt_ptr->next = new_context;
				}else{
					contexts = new_context;
				}
			}
		}
	}

	ctxt_ptr = contexts;
	while(ctxt_ptr){
		mqtt_close_socket(ctxt_ptr);
	}
	close(listensock);

	mqtt_db_close();

	return 0;
}

