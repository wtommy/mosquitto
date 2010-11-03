/*
Copyright (c) 2009,2010, Roger Light <roger@atchoo.org>
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
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef WITH_WRAP
#include <tcpd.h>
#endif
#include <unistd.h>

#include <mqtt3.h>
#include <memory_mosq.h>

static int run;
#ifdef WITH_WRAP
int allow_severity = LOG_INFO;
int deny_severity = LOG_INFO;
#endif

mosquitto_db int_db;

int drop_privileges(mqtt3_config *config);
void handle_sigint(int signal);
void handle_sigusr1(int signal);
void handle_sigusr2(int signal);
static void loop_handle_errors(void);
static void loop_handle_reads_writes(struct pollfd *pollfds);

/* mosquitto shouldn't run as root.
 * This function will attempt to change to an unprivileged user and group if
 * running as root. The user is given in config->user.
 * Returns 1 on failure (unknown user, setuid/setgid failure)
 * Returns 0 on success.
 * Note that setting config->user to "root" does not produce an error, but it
 * strongly discouraged.
 */
int drop_privileges(mqtt3_config *config)
{
#ifndef __CYGWIN__
	struct passwd *pwd;

	if(geteuid() == 0){
		if(config->user){
			pwd = getpwnam(config->user);
			if(!pwd){
				mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid user '%s'.", config->user);
				return 1;
			}
			if(setgid(pwd->pw_gid) == -1){
				mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
				return 1;
			}
			if(setuid(pwd->pw_uid) == -1){
				mqtt3_log_printf(MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
				return 1;
			}
		}
		if(geteuid() == 0 || getegid() == 0){
			mqtt3_log_printf(MOSQ_LOG_WARNING, "Warning: Mosquitto should not be run as root/administrator.");
		}
	}
#endif
	return MOSQ_ERR_SUCCESS;
}

int loop(mqtt3_config *config, int *listensock, int listensock_count, int listener_max)
{
	time_t start_time = time(NULL);
	time_t last_backup = time(NULL);
	time_t last_store_clean = time(NULL);
	time_t now;
	int fdcount;
	sigset_t sigblock, origsig;
	int i;
	struct pollfd *pollfds = NULL;
	int pollfd_count = 0;
	int new_clients = 1;
	int client_max = 0;
	int sock_max = 0;


	sigemptyset(&sigblock);
	sigaddset(&sigblock, SIGINT);

	while(run){
		mqtt3_db_sys_update(&int_db, config->sys_interval, start_time);

		if(new_clients){
			client_max = -1;
			for(i=0; i<int_db.context_count; i++){
				if(int_db.contexts[i] && int_db.contexts[i]->core.sock != -1 && int_db.contexts[i]->core.sock > sock_max){
					client_max = int_db.contexts[i]->core.sock;
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
				mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory.");
				return MOSQ_ERR_NOMEM;
			}
		}

		memset(pollfds, -1, sizeof(struct pollfd)*pollfd_count);

		for(i=0; i<listensock_count; i++){
			pollfds[listensock[i]].fd = listensock[i];
			pollfds[listensock[i]].events = POLLIN | POLLPRI;
			pollfds[listensock[i]].revents = 0;
		}

		now = time(NULL);
		for(i=0; i<int_db.context_count; i++){
			if(int_db.contexts[i]){
				if(int_db.contexts[i]->core.sock != -1){
					if(int_db.contexts[i]->bridge){
						mqtt3_check_keepalive(int_db.contexts[i]);
					}
					if(!(int_db.contexts[i]->core.keepalive) || now - int_db.contexts[i]->core.last_msg_in < int_db.contexts[i]->core.keepalive*3/2){
						if(mqtt3_db_message_write(int_db.contexts[i])){
							// FIXME - do something here.
						}
						pollfds[int_db.contexts[i]->core.sock].fd = int_db.contexts[i]->core.sock;
						pollfds[int_db.contexts[i]->core.sock].events = POLLIN | POLLPRI;
						pollfds[int_db.contexts[i]->core.sock].revents = 0;
						if(int_db.contexts[i]->core.out_packet){
							pollfds[int_db.contexts[i]->core.sock].events |= POLLOUT;
						}
					}else{
						mqtt3_log_printf(MOSQ_LOG_NOTICE, "Client %s has exceeded timeout, disconnecting.", int_db.contexts[i]->core.id);
						/* Client has exceeded keepalive*1.5 */
						if(int_db.contexts[i]->bridge || int_db.contexts[i]->clean_session == false){
							mqtt3_socket_close(int_db.contexts[i]);
						}else{
							mqtt3_context_cleanup(&int_db, int_db.contexts[i], true);
							int_db.contexts[i] = NULL;
						}
					}
				}else{
					if(int_db.contexts[i]->bridge){
						/* Want to try to restart the bridge connection */
						if(!int_db.contexts[i]->bridge->restart_t){
							int_db.contexts[i]->bridge->restart_t = time(NULL)+30;
						}else{
							if(time(NULL) > int_db.contexts[i]->bridge->restart_t){
								int_db.contexts[i]->bridge->restart_t = 0;
								mqtt3_bridge_connect(&int_db, int_db.contexts[i]);
							}
						}
					}else{
						if(int_db.contexts[i]->clean_session == true){
							mqtt3_context_cleanup(&int_db, int_db.contexts[i], true);
							int_db.contexts[i] = NULL;
						}
					}
				}
			}
		}

		mqtt3_db_message_timeout_check(&int_db, config->retry_interval);

		sigprocmask(SIG_SETMASK, &sigblock, &origsig);
		fdcount = poll(pollfds, pollfd_count, 1000);
		sigprocmask(SIG_SETMASK, &origsig, NULL);
		if(fdcount == -1){
			loop_handle_errors();
		}else{
			loop_handle_reads_writes(pollfds);

			for(i=0; i<listensock_count; i++){
				if(pollfds[listensock[i]].revents & (POLLIN | POLLPRI)){
					new_clients = 1;
					while(mqtt3_socket_accept(&int_db.contexts, &int_db.context_count, listensock[i]) != -1){
					}
				}
			}
		}
		if(config->persistence && config->autosave_interval){
			if(last_backup + config->autosave_interval < now){
				mqtt3_db_backup(&int_db, false, false);
				last_backup = time(NULL);
			}
		}
		if(!config->store_clean_interval || last_store_clean + config->store_clean_interval < now){
			mqtt3_db_store_clean(&int_db);
			last_store_clean = time(NULL);
		}
	}

	if(pollfds) _mosquitto_free(pollfds);
	return MOSQ_ERR_SUCCESS;
}

/* Error ocurred, probably an fd has been closed. 
 * Loop through and check them all.
 */
static void loop_handle_errors(void)
{
	struct stat statbuf;
	int i;

	for(i=0; i<int_db.context_count; i++){
		if(int_db.contexts[i] && fstat(int_db.contexts[i]->core.sock, &statbuf) == -1){
			if(errno == EBADF){
				if(int_db.contexts[i]->core.state != mosq_cs_disconnecting){
					mqtt3_log_printf(MOSQ_LOG_NOTICE, "Socket error on client %s, disconnecting.", int_db.contexts[i]->core.id);
					mqtt3_db_client_will_queue(&int_db, int_db.contexts[i]);
				}else{
					mqtt3_log_printf(MOSQ_LOG_NOTICE, "Client %s disconnected.", int_db.contexts[i]->core.id);
				}
				if(int_db.contexts[i]->bridge || int_db.contexts[i]->clean_session == false){
					mqtt3_socket_close(int_db.contexts[i]);
				}else{
					mqtt3_context_cleanup(&int_db, int_db.contexts[i], true);
					int_db.contexts[i] = NULL;
				}
			}
		}
	}
}

static void loop_handle_reads_writes(struct pollfd *pollfds)
{
	int i;

	for(i=0; i<int_db.context_count; i++){
		if(int_db.contexts[i] && int_db.contexts[i]->core.sock != -1){
			if(pollfds[int_db.contexts[i]->core.sock].revents & POLLOUT){
				if(mqtt3_net_write(int_db.contexts[i])){
					if(int_db.contexts[i]->core.state != mosq_cs_disconnecting){
						mqtt3_log_printf(MOSQ_LOG_NOTICE, "Socket write error on client %s, disconnecting.", int_db.contexts[i]->core.id);
						mqtt3_db_client_will_queue(&int_db, int_db.contexts[i]);
					}else{
						mqtt3_log_printf(MOSQ_LOG_NOTICE, "Client %s disconnected.", int_db.contexts[i]->core.id);
					}
					/* Write error or other that means we should disconnect */
					/* Bridges don't get cleaned up because they will reconnect later. */
					if(int_db.contexts[i]->bridge || int_db.contexts[i]->clean_session == false){
						mqtt3_socket_close(int_db.contexts[i]);
					}else{
						mqtt3_context_cleanup(&int_db, int_db.contexts[i], true);
						int_db.contexts[i] = NULL;
					}
				}
			}
		}
		if(int_db.contexts[i] && int_db.contexts[i]->core.sock != -1){
			if(pollfds[int_db.contexts[i]->core.sock].revents & POLLIN){
				if(mqtt3_net_read(&int_db, int_db.contexts[i])){
					if(int_db.contexts[i]->core.state != mosq_cs_disconnecting){
						mqtt3_log_printf(MOSQ_LOG_NOTICE, "Socket read error on client %s, disconnecting.", int_db.contexts[i]->core.id);
						mqtt3_db_client_will_queue(&int_db, int_db.contexts[i]);
					}else{
						mqtt3_log_printf(MOSQ_LOG_NOTICE, "Client %s disconnected.", int_db.contexts[i]->core.id);
					}
					/* Read error or other that means we should disconnect */
					/* Bridges don't get cleaned up because they will reconnect later. */
					if(int_db.contexts[i]->bridge || int_db.contexts[i]->clean_session == false){
						mqtt3_socket_close(int_db.contexts[i]);
					}else{
						mqtt3_context_cleanup(&int_db, int_db.contexts[i], true);
						int_db.contexts[i] = NULL;
					}
				}
			}
		}
	}
}

/* Signal handler for SIGINT and SIGTERM - just stop gracefully. */
void handle_sigint(int signal)
{
	run = 0;
}

/* Signal handler for SIGUSR1 - backup the db. */
void handle_sigusr1(int signal)
{
	mqtt3_db_backup(&int_db, false, false);
}

/* Signal handler for SIGUSR2 - vacuum the db. */
void handle_sigusr2(int signal)
{
	//mqtt3_db_vacuum();
	mqtt3_sub_tree_print(&int_db.subs, 0);
}

int main(int argc, char *argv[])
{
	int *listensock = NULL;
	int listensock_count = 0;
	int listensock_index = 0;
	int *socks, sock_count;
	mqtt3_config config;
	char buf[1024];
	int i, j;
	FILE *pid;
	int listener_max;
	int rc;

	mqtt3_config_init(&config);
	if(mqtt3_config_parse_args(&config, argc, argv)) return 1;

	if(config.daemon){
		switch(fork()){
			case 0:
				break;
			case -1:
				mqtt3_log_printf(MOSQ_LOG_ERR, "Error in fork: %s", strerror(errno));
				return 1;
			default:
				return MOSQ_ERR_SUCCESS;
		}
	}

	if(config.daemon && config.pid_file){
		pid = fopen(config.pid_file, "wt");
		if(pid){
			fprintf(pid, "%d", getpid());
			fclose(pid);
		}else{
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Unable to write pid file.");
			return 1;
		}
	}
	if(drop_privileges(&config)) return 1;

	if(mqtt3_db_open(&config, &int_db)){
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Couldn't open database.");
		return 1;
	}
	/* Initialise logging only after initialising the database in case we're
	 * logging to topics */
	mqtt3_log_init(config.log_type, config.log_dest);
	mqtt3_log_printf(MOSQ_LOG_INFO, "mosquitto version %s (build date %s) starting", VERSION, TIMESTAMP);

	/* Set static $SYS messages */
	snprintf(buf, 1024, "mosquitto version %s", VERSION);
	mqtt3_db_messages_easy_queue(&int_db, NULL, "$SYS/broker/version", 2, strlen(buf), (uint8_t *)buf, 1);
	snprintf(buf, 1024, "%s", TIMESTAMP);
	mqtt3_db_messages_easy_queue(&int_db, NULL, "$SYS/broker/timestamp", 2, strlen(buf), (uint8_t *)buf, 1);
	snprintf(buf, 1024, "%s", "$Revision$"); // Requires hg keyword extension.
	mqtt3_db_messages_easy_queue(&int_db, NULL, "$SYS/broker/changeset", 2, strlen(buf), (uint8_t *)buf, 1);

	listener_max = -1;
	listensock_index = 0;
	for(i=0; i<config.listener_count; i++){
		if(mqtt3_socket_listen(config.listeners[i].host, config.listeners[i].port, &socks, &sock_count)){
			_mosquitto_free(int_db.contexts);
			mqtt3_db_close(&int_db);
			if(config.pid_file){
				remove(config.pid_file);
			}
			return 1;
		}
		listensock_count += sock_count;
		listensock = _mosquitto_realloc(listensock, sizeof(int)*listensock_count);
		if(!listensock){
			_mosquitto_free(int_db.contexts);
			mqtt3_db_close(&int_db);
			if(config.pid_file){
				remove(config.pid_file);
			}
			return 1;
		}
		for(j=0; j<sock_count; j++){
			if(socks[j] < 0){
				_mosquitto_free(int_db.contexts);
				mqtt3_db_close(&int_db);
				if(config.pid_file){
					remove(config.pid_file);
				}
				return 1;
			}
			listensock[listensock_index] = socks[j];
			if(listensock[listensock_index] > listener_max){
				listener_max = listensock[listensock_index];
			}
			listensock_index++;
		}
		_mosquitto_free(socks);
	}

	signal(SIGINT, handle_sigint);
	signal(SIGTERM, handle_sigint);
	signal(SIGUSR1, handle_sigusr1);
	signal(SIGUSR2, handle_sigusr2);
	signal(SIGPIPE, SIG_IGN);

	for(i=0; i<config.bridge_count; i++){
		if(mqtt3_bridge_new(&int_db, &(config.bridges[i]))){
			mqtt3_log_printf(MOSQ_LOG_WARNING, "Warning: Unable to connect to bridge %s.", 
					config.bridges[i].name);
		}
	}
	run = 1;
	rc = loop(&config, listensock, listensock_count, listener_max);

	mqtt3_log_printf(MOSQ_LOG_INFO, "mosquitto version %s terminating", VERSION);
	mqtt3_log_close();

	if(config.persistence && config.autosave_interval){
		mqtt3_db_backup(&int_db, true, true);
	}

	for(i=0; i<int_db.context_count; i++){
		if(int_db.contexts[i]){
			mqtt3_context_cleanup(&int_db, int_db.contexts[i], true);
		}
	}
	_mosquitto_free(int_db.contexts);
	int_db.contexts = NULL;
	mqtt3_db_close(&int_db);

	if(listensock){
		for(i=0; i<listensock_count; i++){
			if(listensock[i] != -1){
				close(listensock[i]);
			}
		}
		_mosquitto_free(listensock);
	}


	if(config.pid_file){
		remove(config.pid_file);
	}

	return rc;
}

