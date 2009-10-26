#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

uint16_t mqtt_publish(int sock, bool dup, char qos, bool retain, const char *topic, const char *payload, int len)
{
}

int main(int argc, char *argv[])
{
	int sock;
	struct sockaddr_in addr;
	char buf;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == -1){
		fprintf(stderr, "Error: %s\n", strerror(errno));
		return 1;
	}

	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(1883);
	inet_aton("127.0.0.1", &(addr.sin_addr));

	if(connect(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) == -1){
		fprintf(stderr, "Error: %s\n", strerror(errno));
		fflush(stderr);
		return 1;
	}

	buf = 16; write(sock, &buf, 1); // CONNECT
	buf = 19; write(sock, &buf, 1); // Remaining length
	buf = 0; write(sock, &buf, 1); // UTF-8 length MSB
	buf = 6; write(sock, &buf, 1); // UTF-8 length LSB
	buf = 'M'; write(sock, &buf, 1);
	buf = 'Q'; write(sock, &buf, 1);
	buf = 'I'; write(sock, &buf, 1);
	buf = 's'; write(sock, &buf, 1);
	buf = 'd'; write(sock, &buf, 1);
	buf = 'p'; write(sock, &buf, 1);
	buf = 3; write(sock, &buf, 1); // Protocol version
	buf = 2; write(sock, &buf, 1); // Clean start, no Will
	buf = 0; write(sock, &buf, 1); // Keep alive MSB 
	buf = 10; write(sock, &buf, 1); // Keep alive LSB
	buf = 0; write(sock, &buf, 1); // UTF-8 length MSB
	buf = 5; write(sock, &buf, 1); // UTF-8 length LSB
	buf = 'R'; write(sock, &buf, 1);
	buf = 'o'; write(sock, &buf, 1);
	buf = 'g'; write(sock, &buf, 1);
	buf = 'e'; write(sock, &buf, 1);
	buf = 'r'; write(sock, &buf, 1);

	read(sock, &buf, 1);
	if(buf == 32){
		// CONNACK
		read(sock, &buf, 1);//Reserved
		read(sock, &buf, 1); // Return code
		printf("%d\n", buf);

		if(buf == 0){
			// Connection accepted
			buf = 48; write(sock, &buf, 1); // PUBLISH
			buf = 8; write(sock, &buf, 1); // Remaining length
			buf = 0; write(sock, &buf, 1); // UTF-8 MSB
			buf = 3; write(sock, &buf, 1); // UTF-8 LSB
			buf = 'a'; write(sock, &buf, 1);
			buf = '/'; write(sock, &buf, 1);
			buf = 'b'; write(sock, &buf, 1);
			buf = 'b'; write(sock, &buf, 1);
			buf = 'o'; write(sock, &buf, 1);
			buf = 'o'; write(sock, &buf, 1);
			read(sock, &buf, 1);
			printf("%d\n", buf);
			read(sock, &buf, 1);
			printf("%d\n", buf);
			read(sock, &buf, 1);
			printf("%d\n", buf);
			read(sock, &buf, 1);
			printf("%d\n", buf);
		}
	}
	sleep(2);
	close(sock);

	return 0;
}

