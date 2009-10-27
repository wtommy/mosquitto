#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <mqtt3.h>

int main(int argc, char *argv[])
{
	int sock;
	unsigned char buf;

	sock = mqtt_connect_socket("127.0.0.1", 1883);
	if(sock == -1){
		fprintf(stderr, "Error: %s\n", strerror(errno));
		return 1;
	}

	mqtt_raw_connect(sock, "Roger", 5, false, 0, false, "", 0, "", 0, 10, false);

	read(sock, &buf, 1);
	printf("%s ", mqtt_command_to_string(buf&0xF0));
	if(buf == 32){
		// CONNACK
		read(sock, &buf, 1); // Remaining length
		printf("%d ", buf);
		read(sock, &buf, 1);//Reserved
		printf("%d ", buf);
		read(sock, &buf, 1); // Return code
		printf("%d\n", buf);

		if(buf == 0){
			// Connection accepted
			mqtt_raw_subscribe(sock, false, "a/b/c", 5, 0);
			read(sock, &buf, 1);
			printf("%s ", mqtt_command_to_string(buf&0xF0));
			read(sock, &buf, 1); // Remaining length
			printf("%d ", buf);
			read(sock, &buf, 1); // Message ID MSB
			printf("%d ", buf);
			read(sock, &buf, 1); // Message ID LSB
			printf("%d ", buf);
			read(sock, &buf, 1); // Granted QoS
			printf("%d\n", buf);


			mqtt_raw_publish(sock, false, 0, false, "a/b/c", 5, "Roger", 5);
			read(sock, &buf, 1); // Should be 00110000 = 48 = PUBLISH
			printf("%s ", mqtt_command_to_string(buf&0xF0));
			read(sock, &buf, 1); // Remaining length (should be 12)
			printf("%d ", buf);
			read(sock, &buf, 1); // Topic MSB
			printf("%d ", buf);
			read(sock, &buf, 1); // Topic LSB
			printf("%d ", buf);
			read(sock, &buf, 1); // Topic
			printf("%c", buf);
			read(sock, &buf, 1);
			printf("%c", buf);
			read(sock, &buf, 1);
			printf("%c", buf);
			read(sock, &buf, 1);
			printf("%c", buf);
			read(sock, &buf, 1);
			printf("%c ", buf);
			read(sock, &buf, 1); // Payload
			printf("%c", buf);
			read(sock, &buf, 1);
			printf("%c", buf);
			read(sock, &buf, 1);
			printf("%c", buf);
			read(sock, &buf, 1);
			printf("%c", buf);
			read(sock, &buf, 1);
			printf("%c\n", buf);
		}
	}

	mqtt_raw_unsubscribe(sock, false, "a/b/c", 5);
	read(sock, &buf, 1);
	printf("%s ", mqtt_command_to_string(buf&0xF0));
	read(sock, &buf, 1); // Remaining length
	printf("%d ", buf);
	read(sock, &buf, 1); // Message ID MSB
	printf("%d ", buf);
	read(sock, &buf, 1); // Message ID LSB
	printf("%d\n", buf);

	mqtt_raw_pingreq(sock);
	read(sock, &buf, 1);
	printf("%s ", mqtt_command_to_string(buf&0xF0));
	read(sock, &buf, 1);
	printf("%d\n", buf);

	sleep(2);
	mqtt_raw_disconnect(sock);
	close(sock);

	return 0;
}

