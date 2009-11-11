#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
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

static int run;

void handle_sigint(int signal)
{
	run = 0;
}

int mqtt3_listen_socket(uint16_t port)
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

mqtt3_context *mqtt3_init_context(int sock)
{
	mqtt3_context *context;

	context = malloc(sizeof(mqtt3_context));
	if(!context) return NULL;
	
	context->next = NULL;
	context->sock = sock;
	context->last_message = time(NULL);
	context->keepalive = 60; /* Default to 60s */
	context->last_mid = 0;
	context->id = NULL;
	context->messages = NULL;

	return context;
}

void mqtt3_cleanup_context(mqtt3_context *context)
{
	if(!context) return;

	if(context->sock != -1){
		mqtt3_close_socket(context);
	}
	if(context->id) free(context->id);
	/* FIXME - clean messages and subscriptions */
	free(context);
}

int handle_read(mqtt3_context *context)
{
	uint8_t byte;

	if(mqtt3_read_byte(context, &byte)) return 1;
	switch(byte&0xF0){
		case CONNECT:
			if(mqtt3_handle_connect(context)) return 1;
			break;
		case DISCONNECT:
			if(mqtt3_handle_disconnect(context)) return 1;
			break;
		case PINGREQ:
			if(mqtt3_handle_pingreq(context)) return 1;
			break;
		case PINGRESP:
			if(mqtt3_handle_pingresp(context)) return 1;
			break;
		case PUBLISH:
			if(mqtt3_handle_publish(context, byte)) return 1;
			break;
		case SUBSCRIBE:
			if(mqtt3_handle_subscribe(context)) return 1;
			break;
		case UNSUBSCRIBE:
			if(mqtt3_handle_unsubscribe(context)) return 1;
			break;
		default:
			printf("Received command: %s (%d)\n", mqtt3_command_to_string(byte&0xF0), byte&0xF0);
			break;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct timespec timeout;
	fd_set readfds, writefds;
	int fdcount; int listensock;
	int new_sock;
	mqtt3_context *contexts = NULL;
	mqtt3_context *ctxt_ptr, *ctxt_last;
	mqtt3_context *new_context;
	mqtt3_context *ctxt_reap;
	int sockmax;

	signal(SIGINT, handle_sigint);

	if(mqtt3_db_open("mosquitto.db")){
		fprintf(stderr, "Error: Couldn't open database.\n");
	}

	listensock = mqtt3_listen_socket(1883);
	if(listensock == -1){
		return 1;
	}

	run = 1;
	while(run){
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
			ctxt_last = NULL;
			while(ctxt_ptr){
				if(ctxt_ptr->sock != -1 && FD_ISSET(ctxt_ptr->sock, &readfds)){
					if(handle_read(ctxt_ptr)){
						printf("Connection error for socket %d\n", ctxt_ptr->sock);
						/* Read error or other that means we should disconnect */
						ctxt_reap = ctxt_ptr;
						if(ctxt_last){
							ctxt_last->next = ctxt_ptr->next;
							ctxt_ptr = ctxt_last;
							mqtt3_cleanup_context(ctxt_reap);
						}else{
							/* In this case, the reaped context is at index 0.
							 * We can't reference index -1, so ctxt_ptr =
							 * ctxt_ptr->next means that index 1 will be
							 * skipped over for index 2. This should get caught
							 * next time round though.
							 */
							contexts = ctxt_ptr->next;
							ctxt_ptr = contexts;
							mqtt3_cleanup_context(ctxt_reap);
							if(!contexts) break;
						}
					}
				}
				ctxt_last = ctxt_ptr;
				ctxt_ptr = ctxt_ptr->next;
			}
			if(FD_ISSET(listensock, &readfds)){
				new_sock = accept(listensock, NULL, 0);
				new_context = mqtt3_init_context(new_sock);
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
		mqtt3_close_socket(ctxt_ptr);
		ctxt_last = ctxt_ptr;
		ctxt_ptr = ctxt_ptr->next;
		mqtt3_cleanup_context(ctxt_last);
	}
	close(listensock);

	mqtt3_db_close();

	return 0;
}

