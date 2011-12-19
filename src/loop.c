/*
Copyright (c) 2009-2011 Roger Light <roger@atchoo.org>
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

#ifndef WIN32
#include <poll.h>
#else
#include <process.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <mqtt3.h>
#include <memory_mosq.h>
#include <util_mosq.h>

extern bool flag_reload;
#ifdef WITH_PERSISTENCE
extern bool flag_db_backup;
#endif
extern bool flag_tree_print;
extern int run;

static void loop_handle_errors(mosquitto_db *db, struct pollfd *pollfds);
static void loop_handle_reads_writes(mosquitto_db *db, struct pollfd *pollfds);

int mosquitto_main_loop(mosquitto_db *db, int *listensock, int listensock_count, int listener_max)
{
	time_t start_time = time(NULL);
	time_t last_backup = time(NULL);
	time_t last_store_clean = time(NULL);
	time_t now;
	int fdcount;
#ifndef WIN32
	sigset_t sigblock, origsig;
#endif
	int i;
	struct pollfd *pollfds = NULL;
	unsigned int pollfd_count = 0;
	int new_clients = 1;
	int client_max = 0;
	unsigned int sock_max = 0;
	char *notification_topic;
	int notification_topic_len;
	uint8_t notification_payload[2];

#ifndef WIN32
	sigemptyset(&sigblock);
	sigaddset(&sigblock, SIGINT);
#endif

	while(run){
		mqtt3_db_sys_update(db, db->config->sys_interval, start_time);

		if(new_clients){
			client_max = -1;
			for(i=0; i<db->context_count; i++){
				if(db->contexts[i] && db->contexts[i]->sock != INVALID_SOCKET && db->contexts[i]->sock > sock_max){
					client_max = db->contexts[i]->sock;
				}
			}
			new_clients = 0;
		}

		if(client_max > listener_max){
			sock_max = client_max;
		}else{
			sock_max = listener_max;
		}
		if(sock_max+1 > pollfd_count){
			pollfd_count = sock_max+1;
			pollfds = _mosquitto_realloc(pollfds, sizeof(struct pollfd)*pollfd_count);
			if(!pollfds){
				_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
				return MOSQ_ERR_NOMEM;
			}
		}

		memset(pollfds, -1, sizeof(struct pollfd)*pollfd_count);

		for(i=0; i<listensock_count; i++){
			pollfds[listensock[i]].fd = listensock[i];
			pollfds[listensock[i]].events = POLLIN;
			pollfds[listensock[i]].revents = 0;
		}

		now = time(NULL);
		for(i=0; i<db->context_count; i++){
			if(db->contexts[i]){
				if(db->contexts[i]->sock != INVALID_SOCKET){
					if(db->contexts[i]->sock > sock_max){
						sock_max = db->contexts[i]->sock;
					}
#ifdef WITH_BRIDGE
					if(db->contexts[i]->bridge){
						_mosquitto_check_keepalive(db->contexts[i]);
					}
#endif
					if(!(db->contexts[i]->keepalive) || now - db->contexts[i]->last_msg_in < (time_t)(db->contexts[i]->keepalive)*3/2){
						if(mqtt3_db_message_write(db->contexts[i]) == MOSQ_ERR_SUCCESS){
							if(db->contexts[i]->sock < pollfd_count){
								pollfds[db->contexts[i]->sock].fd = db->contexts[i]->sock;
								pollfds[db->contexts[i]->sock].events = POLLIN;
								pollfds[db->contexts[i]->sock].revents = 0;
								if(db->contexts[i]->out_packet){
									pollfds[db->contexts[i]->sock].events |= POLLOUT;
								}
							}
						}else{
							mqtt3_context_disconnect(db, i);
						}
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "Client %s has exceeded timeout, disconnecting.", db->contexts[i]->id);
						/* Client has exceeded keepalive*1.5 */
						mqtt3_context_disconnect(db, i);
					}
				}else{
#ifdef WITH_BRIDGE
					if(db->contexts[i]->bridge){
						/* Want to try to restart the bridge connection */
						if(!db->contexts[i]->bridge->restart_t){
							db->contexts[i]->bridge->restart_t = time(NULL)+30;
							if(db->contexts[i]->bridge->notifications){
								notification_topic_len = strlen(db->contexts[i]->id)+strlen("$SYS/broker/connection//state");
								notification_topic = _mosquitto_malloc(sizeof(char)*(notification_topic_len+1));
								if(notification_topic){
									snprintf(notification_topic, notification_topic_len+1, "$SYS/broker/connection/%s/state", db->contexts[i]->id);
									notification_payload[0] = '0';
									notification_payload[1] = '\0';
									mqtt3_db_messages_easy_queue(db, db->contexts[i], notification_topic, 1, 2, (uint8_t *)&notification_payload, 1);
									_mosquitto_free(notification_topic);
								}
							}
						}else{
							if(db->contexts[i]->bridge->start_type == bst_automatic && time(NULL) > db->contexts[i]->bridge->restart_t){
								db->contexts[i]->bridge->restart_t = 0;
								mqtt3_bridge_connect(db, db->contexts[i]);
							}
						}
					}else{
#endif
						if(db->contexts[i]->clean_session == true){
							mqtt3_context_cleanup(db, db->contexts[i], true);
							db->contexts[i] = NULL;
						}
#ifdef WITH_BRIDGE
					}
#endif
				}
			}
		}

		mqtt3_db_message_timeout_check(db, db->config->retry_interval);

#ifndef WIN32
		sigprocmask(SIG_SETMASK, &sigblock, &origsig);
		fdcount = poll(pollfds, pollfd_count, 1000);
		sigprocmask(SIG_SETMASK, &origsig, NULL);
#else
		fdcount = WSAPoll(pollfds, pollfd_count, 1000);
#endif
		if(fdcount == -1){
			loop_handle_errors(db, pollfds);
		}else{
			loop_handle_reads_writes(db, pollfds);

			for(i=0; i<listensock_count; i++){
				if(pollfds[listensock[i]].revents & (POLLIN | POLLPRI)){
					new_clients = 1;
					while(mqtt3_socket_accept(db, listensock[i]) != -1){
					}
				}
			}
		}
#ifdef WITH_PERSISTENCE
		if(db->config->persistence && db->config->autosave_interval){
			if(last_backup + db->config->autosave_interval < now){
				mqtt3_db_backup(db, false, false);
				last_backup = time(NULL);
			}
		}
#endif
		if(!db->config->store_clean_interval || last_store_clean + db->config->store_clean_interval < now){
			mqtt3_db_store_clean(db);
			last_store_clean = time(NULL);
		}
#ifdef WITH_PERSISTENCE
		if(flag_db_backup){
			mqtt3_db_backup(db, false, false);
			flag_db_backup = false;
		}
#endif
		if(flag_reload){
			_mosquitto_log_printf(NULL, MOSQ_LOG_INFO, "Reloading config.");
			mqtt3_config_read(db->config, true);
			mosquitto_security_cleanup(db);
			mosquitto_security_init(db);
			mosquitto_security_apply(db);
			flag_reload = false;
		}
		if(flag_tree_print){
			mqtt3_sub_tree_print(&db->subs, 0);
			flag_tree_print = false;
		}
	}

	if(pollfds) _mosquitto_free(pollfds);
	return MOSQ_ERR_SUCCESS;
}

/* Error ocurred, probably an fd has been closed. 
 * Loop through and check them all.
 */
static void loop_handle_errors(mosquitto_db *db, struct pollfd *pollfds)
{
	int i;

	for(i=0; i<db->context_count; i++){
		if(db->contexts[i] && db->contexts[i]->sock != INVALID_SOCKET){
			if(pollfds[db->contexts[i]->sock].revents & POLLHUP){
				if(db->contexts[i]->state != mosq_cs_disconnecting){
					_mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "Socket error on client %s, disconnecting.", db->contexts[i]->id);
				}else{
					_mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "Client %s disconnected.", db->contexts[i]->id);
				}
				mqtt3_context_disconnect(db, i);
			}
		}
	}
}

static void loop_handle_reads_writes(mosquitto_db *db, struct pollfd *pollfds)
{
	int i;

	for(i=0; i<db->context_count; i++){
		if(db->contexts[i] && db->contexts[i]->sock != INVALID_SOCKET){
			if(pollfds[db->contexts[i]->sock].revents & POLLOUT){
				if(_mosquitto_packet_write(db->contexts[i])){
					if(db->contexts[i]->state != mosq_cs_disconnecting){
						_mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "Socket write error on client %s, disconnecting.", db->contexts[i]->id);
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "Client %s disconnected.", db->contexts[i]->id);
					}
					/* Write error or other that means we should disconnect */
					mqtt3_context_disconnect(db, i);
				}
			}
		}
		if(db->contexts[i] && db->contexts[i]->sock != INVALID_SOCKET){
			if(pollfds[db->contexts[i]->sock].revents & POLLIN){
				if(_mosquitto_packet_read(db, i)){
					if(db->contexts[i]->state != mosq_cs_disconnecting){
						_mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "Socket read error on client %s, disconnecting.", db->contexts[i]->id);
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "Client %s disconnected.", db->contexts[i]->id);
					}
					/* Read error or other that means we should disconnect */
					mqtt3_context_disconnect(db, i);
				}
			}
		}
	}
}

