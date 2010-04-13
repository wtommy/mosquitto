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

int drop_privileges(mqtt3_config *config);
void handle_sigint(int signal);
void handle_sigusr1(int signal);

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
	struct passwd *pwd;

	if(geteuid() == 0){
		if(config->user){
			pwd = getpwnam(config->user);
			if(!pwd){
				mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Invalid user '%s'.", config->user);
				return 1;
			}
			if(setgid(pwd->pw_gid) == -1){
				mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s.", strerror(errno));
				return 1;
			}
			if(setuid(pwd->pw_uid) == -1){
				mqtt3_log_printf(MQTT3_LOG_ERR, "Error: %s.", strerror(errno));
				return 1;
			}
		}
		if(geteuid() == 0 || getegid() == 0){
			mqtt3_log_printf(MQTT3_LOG_WARNING, "Warning: Mosquitto should not be run as root/administrator.");
		}
	}
	return 0;
}

/* Signal handler for SIGINT and SIGTERM - just stop gracefully. */
void handle_sigint(int signal)
{
	run = 0;
}

/* Signal handler for SIGUSR1 - backup the db. */
void handle_sigusr1(int signal)
{
	mqtt3_db_backup(false);
}

int main(int argc, char *argv[])
{
	struct timespec timeout;
	fd_set readfds, writefds;
	sigset_t sigblock;
	int fdcount;
	int *listensock = NULL;
	mqtt3_context **contexts = NULL;
	int context_count;
	int sockmax;
	struct stat statbuf;
	time_t now;
	mqtt3_config config;
	time_t start_time = time(NULL);
	time_t last_backup = time(NULL);
	char buf[1024];
	int i;
	FILE *pid;

	mqtt3_config_init(&config);
	if(mqtt3_config_parse_args(&config, argc, argv)) return 1;

	if(config.daemon){
		switch(fork()){
			case 0:
				break;
			case -1:
				mqtt3_log_printf(MQTT3_LOG_ERR, "Error in fork: %s", strerror(errno));
				return 1;
			default:
				return 0;
		}
	}

	if(config.daemon && config.pid_file){
		pid = fopen(config.pid_file, "wt");
		if(pid){
			fprintf(pid, "%d", getpid());
			fclose(pid);
		}else{
			mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Unable to write pid file.");
			return 1;
		}
	}
	if(drop_privileges(&config)) return 1;

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
	signal(SIGUSR1, handle_sigusr1);
	signal(SIGPIPE, SIG_IGN);

	sigemptyset(&sigblock);
	sigaddset(&sigblock, SIGINT);

	if(mqtt3_db_open(&config)){
		mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Couldn't open database.");
		return 1;
	}
	/* Initialise logging only after initialising the database in case we're
	 * logging to topics */
	mqtt3_log_init(config.log_type, config.log_dest);
	mqtt3_log_printf(MQTT3_LOG_INFO, "mosquitto version %s (build date %s) starting", VERSION, TIMESTAMP);

	/* Set static $SYS messages */
	snprintf(buf, 1024, "mosquitto version %s", VERSION);
	mqtt3_db_messages_easy_queue("$SYS/broker/version", 2, strlen(buf), (uint8_t *)buf, 1);
	snprintf(buf, 1024, "%s", TIMESTAMP);
	mqtt3_db_messages_easy_queue("$SYS/broker/timestamp", 2, strlen(buf), (uint8_t *)buf, 1);

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
		FD_ZERO(&writefds);

		sockmax = 0;
		for(i=0; i<config.iface_count; i++){
			FD_SET(listensock[i], &readfds);
			if(listensock[i] > sockmax){
				sockmax = listensock[i];
			}
		}

		now = time(NULL);
		for(i=0; i<context_count; i++){
			if(contexts[i]){
				if(contexts[i]->sock != -1){
					if(!(contexts[i]->keepalive) || now - contexts[i]->last_msg_in < contexts[i]->keepalive*3/2){
						if(mqtt3_db_message_write(contexts[i])){
							// FIXME - do something here.
						}
						FD_SET(contexts[i]->sock, &readfds);
						if(contexts[i]->sock > sockmax){
							sockmax = contexts[i]->sock;
						}
						if(contexts[i]->out_packet){
							FD_SET(contexts[i]->sock, &writefds);
						}
					}else{
						mqtt3_log_printf(MQTT3_LOG_NOTICE, "Client %s has exceeded timeout, disconnecting.", contexts[i]->id);
						/* Client has exceeded keepalive*1.5 */
						mqtt3_context_cleanup(contexts[i]);
						contexts[i] = NULL;
					}
				}else if(contexts[i]->clean_start){
					mqtt3_context_cleanup(contexts[i]);
					contexts[i] = NULL;
				}
			}
		}

		mqtt3_db_message_timeout_check(config.retry_interval);

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
							mqtt3_log_printf(MQTT3_LOG_NOTICE, "Socket error on client %s, disconnecting.", contexts[i]->id);
							contexts[i]->sock = -1;
							if(!contexts[i]->disconnecting) mqtt3_db_client_will_queue(contexts[i]);
							mqtt3_context_cleanup(contexts[i]);
							contexts[i] = NULL;
						}
					}
				}
			}
		}else{
			for(i=0; i<context_count; i++){
				if(contexts[i] && contexts[i]->sock != -1 && FD_ISSET(contexts[i]->sock, &writefds)){
					if(mqtt3_net_write(contexts[i])){
						mqtt3_log_printf(MQTT3_LOG_NOTICE, "Socket write error on client %s, disconnecting.", contexts[i]->id);
						/* Write error or other that means we should disconnect */
						if(!contexts[i]->disconnecting) mqtt3_db_client_will_queue(contexts[i]);
						mqtt3_context_cleanup(contexts[i]);
						contexts[i] = NULL;
					}
				}
				if(contexts[i] && contexts[i]->sock != -1 && FD_ISSET(contexts[i]->sock, &readfds)){
					if(mqtt3_net_read(contexts[i])){
						mqtt3_log_printf(MQTT3_LOG_NOTICE, "Socket read error on client %s, disconnecting.", contexts[i]->id);
						/* Read error or other that means we should disconnect */
						if(!contexts[i]->disconnecting) mqtt3_db_client_will_queue(contexts[i]);
						mqtt3_context_cleanup(contexts[i]);
						contexts[i] = NULL;
					}
				}
			}
			for(i=0; i<config.iface_count; i++){
				if(FD_ISSET(listensock[i], &readfds)){
					mqtt3_socket_accept(contexts, &context_count, listensock[i]);
				}
			}
		}
		if(config.persistence && config.autosave_interval){
			if(last_backup + config.autosave_interval < now){
				mqtt3_db_backup(false);
				last_backup = time(NULL);
			}
		}
	}

	mqtt3_log_printf(MQTT3_LOG_INFO, "mosquitto version %s terminating", VERSION);

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

	if(config.persistence && config.autosave_interval){
		mqtt3_db_backup(true);
	}
	mqtt3_db_close();

	if(config.pid_file){
		remove(config.pid_file);
	}

	return 0;
}

