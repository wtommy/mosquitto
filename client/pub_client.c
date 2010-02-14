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
#include <client_shared.h>

static char *topic = NULL;
static char *message = NULL;
static int qos = 0;
static int retain = 0;
static mqtt3_context *gcontext;

void my_connack_callback(int result)
{
	if(!result){
		fprintf(stderr, "Connected ok\n");
		mqtt3_raw_publish(gcontext, false, qos, retain, 1, topic, strlen(message), (uint8_t *)message);
	}else{
		fprintf(stderr, "Connect failed\n");
	}
}

void my_net_write_callback(int command)
{
	if(command == PUBLISH){
		mqtt3_raw_disconnect(gcontext);
	}
}

void print_usage(void)
{
	printf("mosquitto_pub is a simple mqtt client that will publish a message on a single topic and exit.\n\n");
	printf("Usage: mosquitto_pub [-h host] [-i id] [-m message] [-p port] [-q qos] [-r] [-t topic]\n\n");
	printf(" -h : mqtt host to connect to. Defaults to localhost.\n");
	printf(" -i : id to use for this client. Defaults to mosquitto_client_ appended with the process id.\n");
	printf(" -m : message payload to send.\n");
	printf(" -p : network port to connect to. Defaults to 1883.\n");
	printf(" -q : quality of service level to use for the message. Defaults to 0.\n");
	printf(" -r : message should be retained.\n");
	printf(" -t : mqtt topic to publish to.\n");
}

int main(int argc, char *argv[])
{
	mqtt3_context *context;
	char id[30];
	int i;
	char *host = "localhost";
	int port = 1883;
	int keepalive = 60;

	sprintf(id, "mosquitto_pub_%d", getpid());

	for(i=1; i<argc; i++){
		if(!strcmp(argv[i], "-p") || !strcmp(argv[i], "--port")){
			if(i==argc-1){
				fprintf(stderr, "Error: -p argument given but no port specified.\n\n");
				print_usage();
				return 1;
			}else{
				port = atoi(argv[i+1]);
				if(port<1 || port>65535){
					fprintf(stderr, "Error: Invalid port given: %d\n", port);
					print_usage();
					return 1;
				}
			}
			i++;
		}else if(!strcmp(argv[i], "-h") || !strcmp(argv[i], "--host")){
			if(i==argc-1){
				fprintf(stderr, "Error: -h argument given but no host specified.\n\n");
				print_usage();
				return 1;
			}else{
				host = argv[i+1];
			}
			i++;
		}else if(!strcmp(argv[i], "-i") || !strcmp(argv[i], "--id")){
			if(i==argc-1){
				fprintf(stderr, "Error: -i argument given but no id specified.\n\n");
				print_usage();
				return 1;
			}else{
				memset(id, 0, 30);
				snprintf(id, 29, "%s", argv[i+1]);
			}
			i++;
		}else if(!strcmp(argv[i], "-m") || !strcmp(argv[i], "--message")){
			if(i==argc-1){
				fprintf(stderr, "Error: -m argument given but no message specified.\n\n");
				print_usage();
				return 1;
			}else{
				message = argv[i+1];
			}
			i++;
		}else if(!strcmp(argv[i], "-q") || !strcmp(argv[i], "--qos")){
			if(i==argc-1){
				fprintf(stderr, "Error: -q argument given but no QoS specified.\n\n");
				print_usage();
				return 1;
			}else{
				qos = atoi(argv[i+1]);
				if(qos<0 || qos>2){
					fprintf(stderr, "Error: Invalid QoS given: %d\n", qos);
					print_usage();
					return 1;
				}
			}
			i++;
		}else if(!strcmp(argv[i], "-r") || !strcmp(argv[i], "--retain")){
			retain = 1;
		}else if(!strcmp(argv[i], "-t") || !strcmp(argv[i], "--topic")){
			if(i==argc-1){
				fprintf(stderr, "Error: -t argument given but no topic specified.\n\n");
				print_usage();
				return 1;
			}else{
				topic = argv[i+1];
			}
			i++;
		}else{
			fprintf(stderr, "Error: Unknown option '%s'.\n",argv[i]);
			print_usage();
			return 1;
		}
	}
	fflush(stdout);
	if(!topic || !message){
		fprintf(stderr, "Error: Both topic and message must be supplied.\n");
		print_usage();
		return 1;
	}

	if(client_init()){
		fprintf(stderr, "Error: Unable to initialise database.\n");
		return 1;
	}
	client_connack_callback = my_connack_callback;
	client_net_write_callback = my_net_write_callback;

	if(client_connect(&context, host, port, id, keepalive)){
		fprintf(stderr, "Unable to connect.\n");
		return 1;
	}
	gcontext = context;

	while(!client_loop(context)){
	}
	client_cleanup();
	return 0;
}
