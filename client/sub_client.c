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

static char **topics = NULL;
static int topic_count = 0;
static int topic_qos = 0;
static mqtt3_context *gcontext;
int verbose = 0;

int my_publish_callback(const char *topic, int qos, uint32_t payloadlen, const uint8_t *payload, int retain)
{
	if(verbose){
		if(payloadlen){
			printf("%s %s\n", topic, payload);
		}else{
			printf("%s (null)\n", topic);
		}
		fflush(stdout);
	}else{
		if(payloadlen){
			printf("%s\n", payload);
			fflush(stdout);
		}
	}

	return 0;
}

void my_connack_callback(int result)
{
	int i;
	if(!result){
		if(topics){
			for(i=0; i<topic_count; i++){
				mqtt3_raw_subscribe(gcontext, false, topics[i], topic_qos);
			}
		}else{
			mqtt3_raw_subscribe(gcontext, false, "#", topic_qos);
		}
	}else{
		fprintf(stderr, "Connect failed\n");
	}
}

void print_usage(void)
{
	printf("mosquitto_sub is a simple mqtt client that will subscribe to a single topic and print all messages it receives.\n\n");
	printf("Usage: mosquitto_sub [-c] [-i id] [-k keepalive] [-p port] [-q qos] [-t topic] [-v]\n\n");
	printf(" -c : disable 'clean session' (store subscription and pending messages when client disconnects).\n");
	printf(" -d : enable debug messages.\n");
	printf(" -h : mqtt host to connect to. Defaults to localhost.\n");
	printf(" -i : id to use for this client. Defaults to mosquitto_sub_ appended with the process id.\n");
	printf(" -k : keep alive in seconds for this client. Defaults to 60.\n");
	printf(" -p : network port to connect to. Defaults to 1883.\n");
	printf(" -q : quality of service level to use for the subscription. Defaults to 0.\n");
	printf(" -t : mqtt topic to subscribe to. Defaults to #.\n");
	printf(" -v : print published messages verbosely.\n");
}

int main(int argc, char *argv[])
{
	mqtt3_context *context;
	char id[30];
	int i;
	char *host = "localhost";
	int port = 1883;
	int keepalive = 60;
	bool clean_session = true;
	bool debug = false;

	sprintf(id, "mosquitto_sub_%d", getpid());

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
		}else if(!strcmp(argv[i], "-c") || !strcmp(argv[i], "--disable-clean-session")){
			clean_session = false;
		}else if(!strcmp(argv[i], "-d") || !strcmp(argv[i], "--debug")){
			debug = true;
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
		}else if(!strcmp(argv[i], "-k") || !strcmp(argv[i], "--keepalive")){
			if(i==argc-1){
				fprintf(stderr, "Error: -k argument given but no keepalive specified.\n\n");
				print_usage();
				return 1;
			}else{
				keepalive = atoi(argv[i+1]);
				if(keepalive>65535){
					fprintf(stderr, "Error: Invalid keepalive given: %d\n", keepalive);
					print_usage();
					return 1;
				}
			}
			i++;
		}else if(!strcmp(argv[i], "-q") || !strcmp(argv[i], "--qos")){
			if(i==argc-1){
				fprintf(stderr, "Error: -q argument given but no QoS specified.\n\n");
				print_usage();
				return 1;
			}else{
				topic_qos = atoi(argv[i+1]);
				if(topic_qos<0 || topic_qos>2){
					fprintf(stderr, "Error: Invalid QoS given: %d\n", topic_qos);
					print_usage();
					return 1;
				}
			}
			i++;
		}else if(!strcmp(argv[i], "-t") || !strcmp(argv[i], "--topic")){
			if(i==argc-1){
				fprintf(stderr, "Error: -t argument given but no topic specified.\n\n");
				print_usage();
				return 1;
			}else{
				topic_count++;
				topics = mqtt3_realloc(topics, topic_count*sizeof(char *));
				topics[topic_count-1] = argv[i+1];
			}
			i++;
		}else if(!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")){
			verbose = 1;
		}else{
			fprintf(stderr, "Error: Unknown option '%s'.\n",argv[i]);
			print_usage();
			return 1;
		}
	}
	if(debug){
		mqtt3_log_init(MQTT3_LOG_DEBUG | MQTT3_LOG_ERR | MQTT3_LOG_WARNING
				| MQTT3_LOG_NOTICE | MQTT3_LOG_INFO, MQTT3_LOG_STDERR);
	}
	if(client_init()){
		fprintf(stderr, "Error: Unable to initialise database.\n");
		return 1;
	}
	client_publish_callback = my_publish_callback;
	client_connack_callback = my_connack_callback;

	if(client_connect(&context, host, port, id, keepalive, clean_session)){
		fprintf(stderr, "Unable to connect.\n");
		return 1;
	}
	gcontext = context;

	while(!client_loop(context)){
	}
	client_cleanup();
	return 0;
}

