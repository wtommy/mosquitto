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

#define MSGMODE_NONE 0
#define MSGMODE_CMD 1
#define MSGMODE_STDIN_LINE 2
#define MSGMODE_STDIN_FILE 3
#define MSGMODE_FILE 4

static char *topic = NULL;
static char *message = NULL;
static long msglen = 0;
static int qos = 0;
static int retain = 0;
static mqtt3_context *gcontext;
static int mode = MSGMODE_NONE;
static FILE *fptr = NULL;

void my_connack_callback(int result)
{
	if(!result){
		switch(mode){
			case MSGMODE_CMD:
			case MSGMODE_FILE:
				mqtt3_raw_publish(gcontext, false, qos, retain, 1, topic, msglen, (uint8_t *)message);
				break;
		}
	}else{
		fprintf(stderr, "Connect failed\n");
	}
}

void my_net_write_callback(int command)
{
	if(qos == 0 && command == PUBLISH){
		mqtt3_raw_disconnect(gcontext);
	}
}

void my_puback_callback(int mid)
{
	mqtt3_raw_disconnect(gcontext);
}

void my_pubcomp_callback(int mid)
{
	mqtt3_raw_disconnect(gcontext);
}

void print_usage(void)
{
	printf("mosquitto_pub is a simple mqtt client that will publish a message on a single topic and exit.\n\n");
	printf("Usage: mosquitto_pub [-h host] [-i id] [-p port] [-q qos] [-r] {-f file | -l | -m message} -t topic\n\n");
	printf(" -f : send the contents of a file as the message.\n");
	printf(" -h : mqtt host to connect to. Defaults to localhost.\n");
	printf(" -i : id to use for this client. Defaults to mosquitto_pub_ appended with the process id.\n");
	printf(" -l : read messages from stdin, sending a separate message for each line.\n");
	printf(" -m : message payload to send.\n");
	printf(" -p : network port to connect to. Defaults to 1883.\n");
	printf(" -q : quality of service level to use for all messages. Defaults to 0.\n");
	printf(" -r : message should be retained.\n");
	printf(" -s : read message from stdin, sending the entire input as a message.\n");
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
	long pos, rlen;

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
		}else if(!strcmp(argv[i], "-f") || !strcmp(argv[i], "--file")){
			if(mode != MSGMODE_NONE){
				fprintf(stderr, "Error: Only one type of message can be sent at once.\n\n");
				print_usage();
				return 1;
			}else if(i==argc-1){
				fprintf(stderr, "Error: -f argument given but no file specified.\n\n");
				print_usage();
				return 1;
			}else{
				fptr = fopen(argv[i+1], "rb");
				if(!fptr){
					fprintf(stderr, "Error: Unable to open file \"%s\".\n", argv[i+1]);
					return 1;
				}
				mode = MSGMODE_FILE;
				fseek(fptr, 0, SEEK_END);
				msglen = ftell(fptr);
				if(msglen > 268435455){
					fclose(fptr);
					fprintf(stderr, "Error: File \"%s\" is too large (>268,435,455 bytes).\n", argv[i+1]);
					return 1;
				}
				fseek(fptr, 0, SEEK_SET);
				message = mqtt3_malloc(msglen);
				if(!message){
					fclose(fptr);
					fprintf(stderr, "Error: Out of memory.\n");
					return 1;
				}
				pos = 0;
				while(pos < msglen){
					rlen = fread(&(message[pos]), sizeof(char), msglen-pos, fptr);
					pos += rlen;
				}
				fclose(fptr);
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
		}else if(!strcmp(argv[i], "-l") || !strcmp(argv[i], "--stdin-line")){
			if(mode != MSGMODE_NONE){
				fprintf(stderr, "Error: Only one type of message can be sent at once.\n\n");
				print_usage();
				return 1;
			}else{
				mode = MSGMODE_STDIN_LINE;
			}
		}else if(!strcmp(argv[i], "-m") || !strcmp(argv[i], "--message")){
			if(mode != MSGMODE_NONE){
				fprintf(stderr, "Error: Only one type of message can be sent at once.\n\n");
				print_usage();
				return 1;
			}else if(i==argc-1){
				fprintf(stderr, "Error: -m argument given but no message specified.\n\n");
				print_usage();
				return 1;
			}else{
				message = argv[i+1];
				msglen = strlen(message);
				mode = MSGMODE_CMD;
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
		}else if(!strcmp(argv[i], "-s") || !strcmp(argv[i], "--stdin-file")){
			if(mode != MSGMODE_NONE){
				fprintf(stderr, "Error: Only one type of message can be sent at once.\n\n");
				print_usage();
				return 1;
			}else{
				mode = MSGMODE_STDIN_FILE;
			}
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
	if(!topic || mode == MSGMODE_NONE){
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
	client_puback_callback = my_puback_callback;
	client_pubcomp_callback = my_pubcomp_callback;

	if(client_connect(&context, host, port, id, keepalive)){
		fprintf(stderr, "Unable to connect.\n");
		return 1;
	}
	gcontext = context;

	while(!client_loop(context)){
	}
	if(message && mode == MSGMODE_FILE){
		mqtt3_free(message);
	}
	client_cleanup();
	return 0;
}
