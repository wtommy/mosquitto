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
#include <sys/stat.h>
#include <unistd.h>

#include <mqtt3.h>

static int run;

void handle_sigint(int signal)
{
	run = 0;
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
		case PUBACK:
			if(mqtt3_handle_puback(context)) return 1;
			break;
		case PUBCOMP:
			if(mqtt3_handle_pubcomp(context)) return 1;
			break;
		case PUBLISH:
			if(mqtt3_handle_publish(context, byte)) return 1;
			break;
		case PUBREC:
			if(mqtt3_handle_pubrec(context)) return 1;
			break;
		case PUBREL:
			if(mqtt3_handle_pubrel(context)) return 1;
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
	sigset_t sigblock;
	int fdcount; int listensock;
	int new_sock;
	mqtt3_context *contexts = NULL;
	mqtt3_context *ctxt_ptr, *ctxt_last, *ctxt_next;
	mqtt3_context *new_context;
	mqtt3_context *ctxt_reap;
	int sockmax;
	struct stat statbuf;
	time_t now;

	signal(SIGINT, handle_sigint);

	sigemptyset(&sigblock);
	sigaddset(&sigblock, SIGINT);

	if(mqtt3_db_open("mosquitto.db")){
		fprintf(stderr, "Error: Couldn't open database.\n");
	}

	listensock = mqtt3_socket_listen(1883);
	if(listensock == -1){
		return 1;
	}

	run = 1;
	while(run){
		FD_ZERO(&readfds);
		FD_SET(listensock, &readfds);

		sockmax = listensock;
		ctxt_ptr = contexts;
		now = time(NULL);
		while(ctxt_ptr){
			if(ctxt_ptr->sock != -1){
				printf("sock: %d\n", ctxt_ptr->sock);
				FD_SET(ctxt_ptr->sock, &readfds);
				if(ctxt_ptr->sock > sockmax){
					sockmax = ctxt_ptr->sock;
				}
				if(now - ctxt_ptr->last_msg_in > ctxt_ptr->keepalive*3/2){
					/* Client has exceeded keepalive*1.5 
					 * Close socket - it is still in the fd set so will get reaped on the 
					 * pselect error. FIXME - Better to remove it properly. */
					close(ctxt_ptr->sock);
				}
			}
			ctxt_ptr = ctxt_ptr->next;
		}

		FD_ZERO(&writefds);
		timeout.tv_sec = 1;
		timeout.tv_nsec = 0;

		fdcount = pselect(sockmax+1, &readfds, &writefds, NULL, &timeout, &sigblock);
		if(fdcount == -1){
			/* Error ocurred, probably an fd has been closed. 
			 * Loop through and check them all.
			 */
			
			if(contexts){
				ctxt_ptr = contexts;
				ctxt_last = NULL;
				while(ctxt_ptr){
					ctxt_next = ctxt_ptr->next;
					if(fstat(ctxt_ptr->sock, &statbuf) == -1){
						if(errno == EBADF){
							ctxt_reap = ctxt_ptr;
							if(ctxt_last){
								ctxt_last->next = ctxt_ptr->next;
							}else{
								contexts = ctxt_ptr->next;
							}
							ctxt_reap->sock = -1;
							mqtt3_context_cleanup(ctxt_reap);
							ctxt_next = contexts;
							ctxt_last = NULL;
						}
					}else{
						ctxt_last = ctxt_ptr;
					}
					ctxt_ptr = ctxt_next;
				}
			}
			fprintf(stderr, "Error in pselect: %s\n", strerror(errno));
		}else if(fdcount == 0){
			// FIXME - update server topics here
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
							mqtt3_context_cleanup(ctxt_reap);
						}else{
							/* In this case, the reaped context is at index 0.
							 * We can't reference index -1, so ctxt_ptr =
							 * ctxt_ptr->next means that index 1 will be
							 * skipped over for index 2. This should get caught
							 * next time round though.
							 */
							contexts = ctxt_ptr->next;
							ctxt_ptr = contexts;
							mqtt3_context_cleanup(ctxt_reap);
							if(!contexts) break;
						}
					}
				}
				ctxt_last = ctxt_ptr;
				ctxt_ptr = ctxt_ptr->next;
			}
			if(FD_ISSET(listensock, &readfds)){
				new_sock = accept(listensock, NULL, 0);
				new_context = mqtt3_context_init(new_sock);
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
		mqtt3_socket_close(ctxt_ptr);
		ctxt_last = ctxt_ptr;
		ctxt_ptr = ctxt_ptr->next;
		mqtt3_context_cleanup(ctxt_last);
	}
	close(listensock);

	mqtt3_db_close();

	return 0;
}

