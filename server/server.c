#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mqtt3.h>

int mqtt_listen_socket(uint16_t port)
{
	int sock;
	struct sockaddr_in addr;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == -1){
		fprintf(stderr, "Error: %s\n", strerror(errno));
		return -1;
	}

	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if(bind(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) == -1){
		fprintf(stderr, "Error: %s\n", strerror(errno));
		fflush(stderr);
		return -1;
	}

	if(listen(sock, 100) == -1){
		fprintf(stderr, "Error: %s\n", strerror(errno));
		fflush(stderr);
		return -1;
	}

	return sock;
}

int main(int argc, char *argv[])
{
	struct timespec timeout;
	fd_set readfds, writefds;
	int fdcount;
	int listensock;
	mqtt_context clientctxt;
	int sockmax;
	uint8_t byte;

	if(mqtt_open_db("mqtt_broker.db")){
		fprintf(stderr, "Error: Couldn't open database.\n");
	}
	clientctxt.sock = -1;

	listensock = mqtt_listen_socket(1883);
	if(listensock == -1){
		return 1;
	}

	while(1){
		FD_ZERO(&readfds);
		FD_SET(listensock, &readfds);
		if(clientctxt.sock != -1){
			FD_SET(clientctxt.sock, &readfds);
		}
		if(clientctxt.sock > listensock){
			sockmax = clientctxt.sock;
		}else{
			sockmax = listensock;
		}

		FD_ZERO(&writefds);
		timeout.tv_sec = 1;
		timeout.tv_nsec = 0;

		fdcount = pselect(sockmax+1, &readfds, &writefds, NULL, &timeout, NULL);
		if(fdcount == -1){
			fprintf(stderr, "Error in pselect: %s\n", strerror(errno));
		}else if(fdcount == 0){
			printf("loop timeout\n");
		}else{
			if(clientctxt.sock != -1 && FD_ISSET(clientctxt.sock, &readfds)){
				byte = mqtt_read_byte(&clientctxt);
				switch(byte&0xF0){
					case CONNECT:
						mqtt_handle_connect(&clientctxt);
						break;
					default:
            			printf("Received command: %s (%d)\n", mqtt_command_to_string(byte&0xF0), byte&0xF0);
						break;
				}
			}
			if(FD_ISSET(listensock, &readfds)){
				clientctxt.sock = accept(listensock, NULL, 0);
			}
		}
	}

	if(clientctxt.sock != -1){
		close(clientctxt.sock);
	}
	close(listensock);

	mqtt_close_db();

	return 0;
}

