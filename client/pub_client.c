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
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>

#include <mosquitto.h>
#include <client_shared.h>

#define MSGMODE_NONE 0
#define MSGMODE_CMD 1
#define MSGMODE_STDIN_LINE 2
#define MSGMODE_STDIN_FILE 3
#define MSGMODE_FILE 4
#define MSGMODE_NULL 5

#define STATUS_CONNECTING 0
#define STATUS_CONNACK_RECVD 1

static char *topic = NULL;
static char *message = NULL;
static long msglen = 0;
static int qos = 0;
static int retain = 0;
static mqtt3_context *gcontext;
static int mode = MSGMODE_NONE;
static int status = STATUS_CONNECTING;
static struct mosquitto *mosq = NULL;
static int mid_sent = 0;

void my_connect_callback(void *obj, int result)
{
	if(!result){
		gcontext->connected = true;
		switch(mode){
			case MSGMODE_CMD:
			case MSGMODE_FILE:
			case MSGMODE_STDIN_FILE:
				mid_sent = mosquitto_publish(mosq, topic, msglen, (uint8_t *)message, qos, retain);
				break;
			case MSGMODE_NULL:
				mid_sent = mosquitto_publish(mosq, topic, 0, NULL, qos, retain);
				break;
			case MSGMODE_STDIN_LINE:
				status = STATUS_CONNACK_RECVD;
				break;
		}
	}else{
		fprintf(stderr, "Connect failed\n");
	}
}

void my_publish_callback(void *obj, int mid)
{
	if(mode != MSGMODE_STDIN_LINE){
		mosquitto_disconnect(mosq);
	}
}

int load_stdin(void)
{
	long pos = 0, rlen;
	char buf[1024];

	mode = MSGMODE_STDIN_FILE;

	while(!feof(stdin)){
		rlen = fread(buf, 1, 1024, stdin);
		message = realloc(message, pos+rlen);
		if(!message){
			fprintf(stderr, "Error: Out of memory.\n");
			return 1;
		}
		memcpy(&(message[pos]), buf, rlen);
		pos += rlen;
	}
	msglen = pos;

	if(!msglen){
		fprintf(stderr, "Error: Zero length input.\n");
		return 1;
	}

	return 0;
}

int load_file(const char *filename)
{
	long pos, rlen;
	FILE *fptr = NULL;

	fptr = fopen(filename, "rb");
	if(!fptr){
		fprintf(stderr, "Error: Unable to open file \"%s\".\n", filename);
		return 1;
	}
	mode = MSGMODE_FILE;
	fseek(fptr, 0, SEEK_END);
	msglen = ftell(fptr);
	if(msglen > 268435455){
		fclose(fptr);
		fprintf(stderr, "Error: File \"%s\" is too large (>268,435,455 bytes).\n", filename);
		return 1;
	}
	if(msglen == 0){
		fclose(fptr);
		fprintf(stderr, "Error: File \"%s\" is empty.\n", filename);
		return 1;
	}
	fseek(fptr, 0, SEEK_SET);
	message = malloc(msglen);
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
	return 0;
}

void print_usage(void)
{
	printf("mosquitto_pub is a simple mqtt client that will publish a message on a single topic and exit.\n\n");
	printf("Usage: mosquitto_pub [-d] [-h host] [-i id] [-p port] [-q qos] [-r] {-f file | -l | -n | -m message} -t topic\n\n");
	printf(" -d : enable debug messages.\n");
	printf(" -f : send the contents of a file as the message.\n");
	printf(" -h : mqtt host to connect to. Defaults to localhost.\n");
	printf(" -i : id to use for this client. Defaults to mosquitto_pub_ appended with the process id.\n");
	printf(" -l : read messages from stdin, sending a separate message for each line.\n");
	printf(" -m : message payload to send.\n");
	printf(" -n : send a null (zero length) message.\n");
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
	int opt;
	char buf[1024];
	bool debug = false;

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
		}else if(!strcmp(argv[i], "-d") || !strcmp(argv[i], "--debug")){
			debug = true;
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
				if(load_file(argv[i+1])) return 1;
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
				opt = fcntl(fileno(stdin), F_GETFL, 0);
				if(opt == -1 || fcntl(fileno(stdin), F_SETFL, opt | O_NONBLOCK) == -1){
					fprintf(stderr, "Error: Unable to set stdin to non-blocking.\n");
					return 1;
				}
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
		}else if(!strcmp(argv[i], "-n") || !strcmp(argv[i], "--null-message")){
			if(mode != MSGMODE_NONE){
				fprintf(stderr, "Error: Only one type of message can be sent at once.\n\n");
				print_usage();
				return 1;
			}else{
				mode = MSGMODE_NULL;
			}
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
				if(load_stdin()) return 1;
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
	mosquitto_lib_init();
	mosq = mosquitto_new(NULL);
	if(!mosq){
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}
	if(debug){
		mqtt3_log_init(MQTT3_LOG_DEBUG | MQTT3_LOG_ERR | MQTT3_LOG_WARNING
				| MQTT3_LOG_NOTICE | MQTT3_LOG_INFO, MQTT3_LOG_STDERR);
	}

	if(!topic || mode == MSGMODE_NONE){
		fprintf(stderr, "Error: Both topic and message must be supplied.\n");
		print_usage();
		return 1;
	}

	mosq->on_connect = my_connect_callback;
	mosq->on_publish = my_publish_callback;

	if(client_connect(&context, host, port, id, keepalive, true)){
		fprintf(stderr, "Unable to connect.\n");
		return 1;
	}
	gcontext = context;

	while(!mosquitto_loop(mosq)){
		if(mode == MSGMODE_STDIN_LINE && status == STATUS_CONNACK_RECVD){
			if(fgets(buf, 1024, stdin)){
				buf[strlen(buf)-1] = '\0';
				mid_sent = mosquitto_publish(mosq, topic, strlen(buf), (uint8_t *)buf, qos, retain);
			}else if(feof(stdin)){
				mosquitto_disconnect(mosq);
			}
		}
	}
	if(message && mode == MSGMODE_FILE){
		free(message);
	}
	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();
	return 0;
}
