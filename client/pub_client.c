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


#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#else
#include <process.h>
#define snprintf sprintf_s
#endif

#include <mosquitto.h>

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
static int mode = MSGMODE_NONE;
static int status = STATUS_CONNECTING;
static uint16_t mid_sent = 0;
static bool connected = true;
static char *username = NULL;
static char *password = NULL;
static bool disconnect_sent = false;
static bool quiet = false;

void my_connect_callback(void *obj, int result)
{
	struct mosquitto *mosq = obj;
	int rc = MOSQ_ERR_SUCCESS;

	if(!result){
		switch(mode){
			case MSGMODE_CMD:
			case MSGMODE_FILE:
			case MSGMODE_STDIN_FILE:
				rc = mosquitto_publish(mosq, &mid_sent, topic, msglen, (uint8_t *)message, qos, retain);
				break;
			case MSGMODE_NULL:
				rc = mosquitto_publish(mosq, &mid_sent, topic, 0, NULL, qos, retain);
				break;
			case MSGMODE_STDIN_LINE:
				status = STATUS_CONNACK_RECVD;
				break;
		}
		if(rc){
			if(!quiet) fprintf(stderr, "Error: Publish returned %d, disconnecting.\n", rc);
			mosquitto_disconnect(mosq);
		}
	}else{
		switch(result){
			case 1:
				if(!quiet) fprintf(stderr, "Connection Refused: unacceptable protocol version\n");
				break;
			case 2:
				if(!quiet) fprintf(stderr, "Connection Refused: identifier rejected\n");
				break;
			case 3:
				if(!quiet) fprintf(stderr, "Connection Refused: broker unavailable\n");
				break;
			case 4:
				if(!quiet) fprintf(stderr, "Connection Refused: bad user name or password\n");
				break;
			case 5:
				if(!quiet) fprintf(stderr, "Connection Refused: not authorised\n");
				break;
			default:
				if(!quiet) fprintf(stderr, "Connection Refused: unknown reason\n");
				break;
		}
	}
}

void my_disconnect_callback(void *obj)
{
	connected = false;
}

void my_publish_callback(void *obj, uint16_t mid)
{
	struct mosquitto *mosq = obj;

	if(mode != MSGMODE_STDIN_LINE && disconnect_sent == false){
		mosquitto_disconnect(mosq);
		disconnect_sent = true;
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
			if(!quiet) fprintf(stderr, "Error: Out of memory.\n");
			return 1;
		}
		memcpy(&(message[pos]), buf, rlen);
		pos += rlen;
	}
	msglen = pos;

	if(!msglen){
		if(!quiet) fprintf(stderr, "Error: Zero length input.\n");
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
		if(!quiet) fprintf(stderr, "Error: Unable to open file \"%s\".\n", filename);
		return 1;
	}
	mode = MSGMODE_FILE;
	fseek(fptr, 0, SEEK_END);
	msglen = ftell(fptr);
	if(msglen > 268435455){
		fclose(fptr);
		if(!quiet) fprintf(stderr, "Error: File \"%s\" is too large (>268,435,455 bytes).\n", filename);
		return 1;
	}
	if(msglen == 0){
		fclose(fptr);
		if(!quiet) fprintf(stderr, "Error: File \"%s\" is empty.\n", filename);
		return 1;
	}
	fseek(fptr, 0, SEEK_SET);
	message = malloc(msglen);
	if(!message){
		fclose(fptr);
		if(!quiet) fprintf(stderr, "Error: Out of memory.\n");
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
	printf("Usage: mosquitto_pub [-h host] [-i id] [-p port] [-q qos] [-r] {-f file | -l | -n | -m message} -t topic\n");
	printf("                     [-d] [--quiet]\n");
	printf("                     [-u username [--pw password]]\n");
	printf("                     [--will-topic [--will-payload payload] [--will-qos qos] [--will-retain]]\n\n");
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
	printf(" -u : provide a username (requires MQTT 3.1 broker)\n");
	printf(" --pw : provide a password (requires MQTT 3.1 broker)\n");
	printf(" --quiet : don't print error messages.\n");
	printf(" --will-payload : payload for the client Will, which is sent by the broker in case of\n");
	printf("                  unexpected disconnection. If not given and will-topic is set, a zero\n");
	printf("                  length message will be sent.\n");
	printf(" --will-qos : QoS level for the client Will.\n");
	printf(" --will-retain : if given, make the client Will retained.\n");
	printf(" --will-topic : the topic on which to publish the client Will.\n");
	printf("\nSee http://mosquitto.org/ for more information.\n\n");
}

int main(int argc, char *argv[])
{
	char *id = NULL;
	int i;
	char *host = "localhost";
	int port = 1883;
	int keepalive = 60;
	int opt;
	char buf[1024];
	bool debug = false;
	struct mosquitto *mosq = NULL;
	int rc;
	int rc2;

	uint8_t *will_payload = NULL;
	long will_payloadlen = 0;
	int will_qos = 0;
	bool will_retain = false;
	char *will_topic = NULL;

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
				id = argv[i+1];
			}
			i++;
		}else if(!strcmp(argv[i], "-l") || !strcmp(argv[i], "--stdin-line")){
			if(mode != MSGMODE_NONE){
				fprintf(stderr, "Error: Only one type of message can be sent at once.\n\n");
				print_usage();
				return 1;
			}else{
				mode = MSGMODE_STDIN_LINE;
#ifndef WIN32
				opt = fcntl(fileno(stdin), F_GETFL, 0);
				if(opt == -1 || fcntl(fileno(stdin), F_SETFL, opt | O_NONBLOCK) == -1){
					fprintf(stderr, "Error: Unable to set stdin to non-blocking.\n");
					return 1;
				}
#endif
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
		}else if(!strcmp(argv[i], "--quiet")){
			quiet = true;
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
		}else if(!strcmp(argv[i], "-u") || !strcmp(argv[i], "--username")){
			if(i==argc-1){
				fprintf(stderr, "Error: -u argument given but no username specified.\n\n");
				print_usage();
				return 1;
			}else{
				username = argv[i+1];
			}
			i++;
		}else if(!strcmp(argv[i], "--pw")){
			if(i==argc-1){
				fprintf(stderr, "Error: --pw argument given but no password specified.\n\n");
				print_usage();
				return 1;
			}else{
				password = argv[i+1];
			}
			i++;
		}else if(!strcmp(argv[i], "--will-payload")){
			if(i==argc-1){
				fprintf(stderr, "Error: --will-payload argument given but no will payload specified.\n\n");
				print_usage();
				return 1;
			}else{
				will_payload = (uint8_t *)argv[i+1];
				will_payloadlen = strlen((char *)will_payload);
			}
			i++;
		}else if(!strcmp(argv[i], "--will-qos")){
			if(i==argc-1){
				fprintf(stderr, "Error: --will-qos argument given but no will QoS specified.\n\n");
				print_usage();
				return 1;
			}else{
				will_qos = atoi(argv[i+1]);
				if(will_qos < 0 || will_qos > 2){
					fprintf(stderr, "Error: Invalid will QoS %d.\n\n", will_qos);
					return 1;
				}
			}
			i++;
		}else if(!strcmp(argv[i], "--will-retain")){
			will_retain = true;
		}else if(!strcmp(argv[i], "--will-topic")){
			if(i==argc-1){
				fprintf(stderr, "Error: --will-topic argument given but no will topic specified.\n\n");
				print_usage();
				return 1;
			}else{
				will_topic = argv[i+1];
			}
			i++;
		}else{
			fprintf(stderr, "Error: Unknown option '%s'.\n",argv[i]);
			print_usage();
			return 1;
		}
	}
	if(!id){
		id = malloc(30);
		if(!id){
			if(!quiet) fprintf(stderr, "Error: Out of memory.\n");
			return 1;
		}
		snprintf(id, 30, "mosquitto_pub_%d", getpid());
	}

	if(!topic || mode == MSGMODE_NONE){
		fprintf(stderr, "Error: Both topic and message must be supplied.\n");
		print_usage();
		return 1;
	}

	if(will_payload && !will_topic){
		fprintf(stderr, "Error: Will payload given, but no will topic given.\n");
		print_usage();
		return 1;
	}
	if(will_retain && !will_topic){
		fprintf(stderr, "Error: Will retain given, but no will topic given.\n");
		print_usage();
		return 1;
	}
	if(password && !username){
		if(!quiet) fprintf(stderr, "Warning: Not using password since username not set.\n");
	}
	mosquitto_lib_init();
	mosq = mosquitto_new(id, NULL);
	if(!mosq){
		if(!quiet) fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}
	if(debug){
		mosquitto_log_init(mosq, MOSQ_LOG_DEBUG | MOSQ_LOG_ERR | MOSQ_LOG_WARNING
				| MOSQ_LOG_NOTICE | MOSQ_LOG_INFO, MOSQ_LOG_STDERR);
	}
	if(will_topic && mosquitto_will_set(mosq, true, will_topic, will_payloadlen, will_payload, will_qos, will_retain)){
		if(!quiet) fprintf(stderr, "Error: Problem setting will.\n");
		return 1;
	}
	if(username && mosquitto_username_pw_set(mosq, username, password)){
		if(!quiet) fprintf(stderr, "Error: Problem setting username and password.\n");
		return 1;
	}

	mosquitto_connect_callback_set(mosq, my_connect_callback);
	mosquitto_disconnect_callback_set(mosq, my_disconnect_callback);
	mosquitto_publish_callback_set(mosq, my_publish_callback);

	rc = mosquitto_connect(mosq, host, port, keepalive, true);
	if(rc){
		if(!quiet) fprintf(stderr, "Unable to connect (%d).\n", rc);
		return rc;
	}

	do{
		if(mode == MSGMODE_STDIN_LINE && status == STATUS_CONNACK_RECVD){
			if(fgets(buf, 1024, stdin)){
				buf[strlen(buf)-1] = '\0';
				rc2 = mosquitto_publish(mosq, &mid_sent, topic, strlen(buf), (uint8_t *)buf, qos, retain);
				if(rc2){
					if(!quiet) fprintf(stderr, "Error: Publish returned %d, disconnecting.\n", rc2);
					mosquitto_disconnect(mosq);
				}
			}else if(feof(stdin) && disconnect_sent == false){
				mosquitto_disconnect(mosq);
				disconnect_sent = true;
			}
		}
		rc = mosquitto_loop(mosq, -1);
	}while(rc == MOSQ_ERR_SUCCESS && connected);

	if(message && mode == MSGMODE_FILE){
		free(message);
	}
	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();
	return rc;
}
