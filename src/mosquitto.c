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

#include <config.h>

#include <errno.h>
#include <netinet/in.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#ifdef WITH_WRAP
#include <tcpd.h>
#endif
#include <unistd.h>

#include <mqtt3.h>

static int run;
#ifdef WITH_WRAP
int allow_severity = LOG_INFO;
int deny_severity = LOG_INFO;
#endif

int drop_privileges(mqtt3_config *config)
{
	struct passwd *pwd;

	if(geteuid() == 0){
		if(config->user){
			pwd = getpwnam(config->user);
			if(!pwd){
				mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Invalid user '%s'.\n", config->user);
				return 1;
			}
			if(setgid(pwd->pw_gid) == -1){
				mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s.\n", strerror(errno));
				return 1;
			}
			if(setuid(pwd->pw_uid) == -1){
				mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s.\n", strerror(errno));
				return 1;
			}
		}
		if(geteuid() == 0 || getegid() == 0){
			mqtt3_log_printf(MQTT3_LOG_WARNING, "Warning: Mosquitto should not be run as root/administrator.\n");
		}
	}
	return 0;
}

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
			/* If we don't recognise the command, return an error straight away. */
			return 1;
			break;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct timespec timeout;
	fd_set readfds, writefds;
	sigset_t sigblock;
	int fdcount;
	int *listensock = NULL;
	int new_sock;
	mqtt3_context **contexts = NULL;
	mqtt3_context **tmp_contexts = NULL;
	int context_count;
	mqtt3_context *new_context;
	int sockmax;
	struct stat statbuf;
	time_t now;
	mqtt3_config config;
	time_t start_time = time(NULL);
	char buf[1024];
	int i;
	FILE *pid;
#ifdef WITH_WRAP
	struct request_info wrap_req;
#endif

	mqtt3_config_init(&config);
	if(mqtt3_config_parse_args(&config, argc, argv)) return 1;
	/* Initialise logging immediately after loading the config */
	mqtt3_log_init(config.log_priorities, config.log_dest);
	if(drop_privileges(&config)) return 1;

	if(config.daemon){
		switch(fork()){
			case 0:
				break;
			case -1:
				mqtt3_log_printf(MQTT3_LOG_ERR, "Error in fork: %s\n", strerror(errno));
				return 1;
			default:
				return 0;
		}
		if(config.pid_file){
			pid = fopen(config.pid_file, "wt");
			if(pid){
				fprintf(pid, "%d", getpid());
				fclose(pid);
			}else{
				mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Unable to write pid file.\n");
				return 1;
			}
		}
	}

	/* Preallocate enough memory for 100 connected clients.
	 * This will grow in size once >100 clients connect.
	 * Setting it to 100 here prevents excessive allocations.
	 */
	context_count = 100;
	contexts = mqtt3_malloc(sizeof(mqtt3_context*)*context_count);
	if(!contexts) return 1;
	for(i=0; i<context_count; i++){
		contexts[i] = NULL;
	}

	signal(SIGINT, handle_sigint);
	signal(SIGTERM, handle_sigint);
	signal(SIGPIPE, SIG_IGN);

	sigemptyset(&sigblock);
	sigaddset(&sigblock, SIGINT);

	if(config.persistence){
		if(mqtt3_db_open(config.persistence_location, "mosquitto.db")){
			mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Couldn't open database.\n");
			return 1;
		}
	}else{
		if(mqtt3_db_open(NULL, ":memory:")){
			mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Couldn't open database.\n");
			return 1;
		}
	}

	/* Set static $SYS messages */
	snprintf(buf, 1024, "mosquitto version %s (build date %s)", VERSION, BUILDDATE);
	mqtt3_db_messages_queue("$SYS/broker/version", 2, strlen(buf), (uint8_t *)buf, 1);

	listensock = mqtt3_malloc(sizeof(int)*config.iface_count);
	for(i=0; i<config.iface_count; i++){
		if(config.iface[i].iface){
			listensock[i] = mqtt3_socket_listen_if(config.iface[i].iface, config.iface[i].port);
		}else{
			listensock[i] = mqtt3_socket_listen(config.iface[i].port);
		}
		if(listensock[i] == -1){
			mqtt3_free(contexts);
			mqtt3_db_close();
			if(config.pid_file){
				remove(config.pid_file);
			}
			return 1;
		}
	}

	run = 1;
	while(run){
		mqtt3_db_sys_update(config.sys_interval, start_time);

		FD_ZERO(&readfds);

		sockmax = 0;
		for(i=0; i<config.iface_count; i++){
			FD_SET(listensock[i], &readfds);
			if(listensock[i] > sockmax){
				sockmax = listensock[i];
			}
		}

		now = time(NULL);
		for(i=0; i<context_count; i++){
			if(contexts[i] && contexts[i]->sock != -1){
				FD_SET(contexts[i]->sock, &readfds);
				if(contexts[i]->sock > sockmax){
					sockmax = contexts[i]->sock;
				}
				if(now - contexts[i]->last_msg_in > contexts[i]->keepalive*3/2){
					/* Client has exceeded keepalive*1.5 
					 * Close socket - it is still in the fd set so will get reaped on the 
					 * pselect error. FIXME - Better to remove it properly. */
					close(contexts[i]->sock);
				}
			}
		}

		mqtt3_db_message_timeout_check(config.msg_timeout);
		mqtt3_db_outgoing_check(&writefds, &sockmax);

		timeout.tv_sec = 1;
		timeout.tv_nsec = 0;

		fdcount = pselect(sockmax+1, &readfds, &writefds, NULL, &timeout, &sigblock);
		if(fdcount == -1){
			/* Error ocurred, probably an fd has been closed. 
			 * Loop through and check them all.
			 */
			
			if(contexts){
				for(i=0; i<context_count; i++){
					if(contexts[i] && fstat(contexts[i]->sock, &statbuf) == -1){
						if(errno == EBADF){
							contexts[i]->sock = -1;
							mqtt3_db_client_will_queue(contexts[i]);
							mqtt3_context_cleanup(contexts[i]);
							contexts[i] = NULL;
						}
					}
				}
			}
		}else{
			for(i=0; i<context_count; i++){
				if(contexts[i] && contexts[i]->sock != -1 && FD_ISSET(contexts[i]->sock, &writefds)){
					if(mqtt3_db_message_write(contexts[i])){
						// FIXME - do something here.
					}
				}
				if(contexts[i] && contexts[i]->sock != -1 && FD_ISSET(contexts[i]->sock, &readfds)){
					if(handle_read(contexts[i])){
						/* Read error or other that means we should disconnect */
						mqtt3_db_client_will_queue(contexts[i]);
						mqtt3_context_cleanup(contexts[i]);
						contexts[i] = NULL;
					}
				}
			}
			for(i=0; i<config.iface_count; i++){
				if(FD_ISSET(listensock[i], &readfds)){
					new_sock = accept(listensock[i], NULL, 0);
#ifdef WITH_WRAP
					/* Use tcpd / libwrap to determine whether a connection is allowed. */
					request_init(&wrap_req, RQ_FILE, new_sock, RQ_DAEMON, "mosquitto", 0);
					fromhost(&wrap_req);
					if(!hosts_access(&wrap_req)){
						/* Access is denied */
						close(new_sock);
					}else{
#endif
						new_context = mqtt3_context_init(new_sock);
						for(i=0; i<context_count; i++){
							if(contexts[i] == NULL){
								contexts[i] = new_context;
								break;
							}
						}
						if(i==context_count){
							context_count++;
							tmp_contexts = mqtt3_realloc(contexts, sizeof(mqtt3_context*)*context_count);
							if(tmp_contexts){
								contexts = tmp_contexts;
								contexts[context_count-1] = new_context;
							}
						}
#ifdef WITH_WRAP
					}
#endif
				}
			}
		}
	}

	 mqtt3_log_close();

	for(i=0; i<context_count; i++){
		if(contexts[i]){
			mqtt3_context_cleanup(contexts[i]);
		}
	}
	mqtt3_free(contexts);

	if(listensock){
		for(i=0; i<config.iface_count; i++){
			if(listensock[i] != -1){
				close(listensock[i]);
			}
		}
		mqtt3_free(listensock);
	}

	mqtt3_db_close();

	if(config.pid_file){
		remove(config.pid_file);
	}

	return 0;
}

