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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>

#include <mqtt3.h>

static char *topic = NULL;
static int topic_qos = 0;
static mqtt3_context *gcontext;

int client_connect(mqtt3_context **context, const char *host, int port, const char *id, int keepalive)
{
	int sock;

	if(!context || !host || !id) return 1;

	sock = mqtt3_socket_connect(host, port);
	*context = mqtt3_context_init(sock);
	if((*context)->sock == -1){
		return 1;
	}

	mqtt3_raw_connect(*context, id,
			/*will*/ false, /*will qos*/ 0, /*will retain*/ false, /*will topic*/ NULL, /*will msg*/ NULL,
			keepalive, /*cleanstart*/true);
	return 0;
}

void mqtt3_check_keepalive(mqtt3_context *context)
{
	if(time(NULL) - context->last_msg_out >= context->keepalive){
		mqtt3_raw_pingreq(context);
	}
}

int my_publish_callback(const char *topic, int qos, uint32_t payloadlen, const uint8_t *payload, int retain)
{
	printf("%s (QoS: %d): \"%s\"\n", topic, qos, payload);

	return 0;
}

void my_connack_callback(int result)
{
	if(!result){
		printf("Connected ok\n");
		mqtt3_raw_subscribe(gcontext, false, topic, topic_qos);
	}else{
		printf("Connect failed\n");
	}
}

int client_loop(mqtt3_context *context)
{
	struct timespec timeout;
	fd_set readfds, writefds;
	int fdcount;

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
				fprintf(stderr, "Read error on socket.\n");
				mqtt3_socket_close(context);
				return 1;
			}
		}
		if(FD_ISSET(context->sock, &writefds)){
			if(mqtt3_net_write(context)){
				fprintf(stderr, "Write error on socket.\n");
				mqtt3_socket_close(context);
				return 1;
			}
		}
	}
	mqtt3_check_keepalive(context);

	return 0;
}

int main(int argc, char *argv[])
{
	mqtt3_context *context;
	char id[30];
	int i;
	char *host = "localhost";
	int port = 1883;

	sprintf(id, "mosquitto_client_%d", getpid());
	topic = "#";

	for(i=1; i<argc; i++){
		if(!strcmp(argv[i], "-p") || !strcmp(argv[i], "--port")){
			if(i==argc-1){
				fprintf(stderr, "Error: -p argument given but no port specified.\n\n");
				return 1;
			}else{
				port = atoi(argv[i+1]);
				if(port<1 || port>65535){
					fprintf(stderr, "Error: Invalid port given: %d\n", port);
					return 1;
				}
			}
			i+=2;
		}else if(!strcmp(argv[i], "-h") || !strcmp(argv[i], "--host")){
			if(i==argc-1){
				fprintf(stderr, "Error: -h argument given but no host specified.\n\n");
				return 1;
			}else{
				host = argv[i+1];
			}
			i+=2;
		}else if(!strcmp(argv[i], "-i") || !strcmp(argv[i], "--id")){
			if(i==argc-1){
				fprintf(stderr, "Error: -i argument given but no id specified.\n\n");
				return 1;
			}else{
				memset(id, 0, 30);
				snprintf(id, 29, "%s", argv[i+1]);
			}
			i+=2;
		}else if(!strcmp(argv[i], "-t") || !strcmp(argv[i], "--topic")){
			if(i==argc-1){
				fprintf(stderr, "Error: -t argument given but no topic specified.\n\n");
				return 1;
			}else{
				topic = argv[i+1];
			}
			i+=2;
		}
	}
	client_publish_callback = my_publish_callback;
	client_connack_callback = my_connack_callback;

	if(client_connect(&context, host, port, id, 60)){
		fprintf(stderr, "Unable to connect.\n");
		return 1;
	}
	gcontext = context;

	while(!client_loop(context)){
	}
	return 0;
}

