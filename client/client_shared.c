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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mqtt3.h>

int client_init(void)
{
	mqtt3_config config;

	config.persistence = 0;
	config.ext_sqlite_regex = NULL;

	return mqtt3_db_open(&config);
}

int client_connect(mqtt3_context **context, const char *host, int port, const char *id, int keepalive, bool clean_session)
{
	int sock;

	if(!context || !host || !id) return 1;

	sock = mqtt3_socket_connect(host, port);
	*context = mqtt3_context_init(sock);
	if((*context)->sock == -1){
		return 1;
	}

	(*context)->id = mqtt3_strdup(id);
	mqtt3_raw_connect(*context, id,
			/*will*/ false, /*will qos*/ 0, /*will retain*/ false, /*will topic*/ NULL, /*will msg*/ NULL,
			keepalive, clean_session);
	return 0;
}

int client_loop(mqtt3_context *context)
{
	struct timespec timeout;
	fd_set readfds, writefds;
	int fdcount;

	if(!context || context->sock < 0){
		return 1;
	}
	FD_ZERO(&readfds);
	FD_SET(context->sock, &readfds);
	FD_ZERO(&writefds);
	if(context->out_packet){
		FD_SET(context->sock, &writefds);
	}
	timeout.tv_sec = 1;
	timeout.tv_nsec = 0;

	fdcount = pselect(context->sock+1, &readfds, &writefds, NULL, &timeout, NULL);
	if(fdcount == -1){
		fprintf(stderr, "Error in pselect: %s\n", strerror(errno));
		return 1;
	}else{
		if(FD_ISSET(context->sock, &readfds)){
			if(mqtt3_net_read(context)){
				mqtt3_socket_close(context);
				return 1;
			}
		}
		if(FD_ISSET(context->sock, &writefds)){
			if(mqtt3_net_write(context)){
				mqtt3_socket_close(context);
				return 1;
			}
		}
	}
	mqtt3_check_keepalive(context);

	return 0;
}

