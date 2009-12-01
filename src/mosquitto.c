/*
Copyright (c) 2009, Roger Light <roger@atchoo.org>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of mosquitto nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

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
			// FIXME - do something?
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
	int daemon = 1;

	if(daemon){
		switch(fork()){
			case 0:
				break;
			case -1:
				fprintf(stderr, "Error in fork: %s\n", strerror(errno));
				return 1;
			default:
				return 0;
		}
	}

	signal(SIGINT, handle_sigint);
	signal(SIGPIPE, SIG_IGN);

	sigemptyset(&sigblock);
	sigaddset(&sigblock, SIGINT);

	if(mqtt3_db_open("mosquitto.db")){
		fprintf(stderr, "Error: Couldn't open database.\n");
		return 1;
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

		mqtt3_db_message_timeout_check(5);
		mqtt3_db_outgoing_check(&writefds, &sockmax);

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
		}else if(fdcount == 0){
			// FIXME - update server topics here
		}else{
			ctxt_ptr = contexts;
			ctxt_last = NULL;
			while(ctxt_ptr){
				if(ctxt_ptr->sock != -1 && FD_ISSET(ctxt_ptr->sock, &writefds)){
					if(mqtt3_db_message_write(ctxt_ptr)){
						// FIXME - do something here.
					}
				}
				if(ctxt_ptr->sock != -1 && FD_ISSET(ctxt_ptr->sock, &readfds)){
					if(handle_read(ctxt_ptr)){
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

