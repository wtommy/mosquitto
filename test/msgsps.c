/* This provides a crude manner of testing the performance of a broker in messages/s. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <mosquitto.h>

#define MESSAGE_COUNT 20000

static bool run = true;
static int message_count = 0;
static time_t start = 0, stop = 0;

void my_connect_callback(void *obj, int rc)
{
	printf("rc: %d\n", rc);
	start = time(NULL);
}

void my_disconnect_callback(void *obj)
{
	run = false;
}

void my_publish_callback(void *obj, uint16_t mid)
{
	message_count++;
//	printf("%d ", message_count);
	if(message_count == MESSAGE_COUNT){
		stop = time(NULL);
		mosquitto_disconnect((struct mosquitto *)obj);
	}
}

int main(int argc, char *argv[])
{
	struct mosquitto *mosq;
	int i;

	mosquitto_lib_init();

	mosq = mosquitto_new("perftest", NULL);
	mosquitto_connect_callback_set(mosq, my_connect_callback);
	mosquitto_disconnect_callback_set(mosq, my_disconnect_callback);
	mosquitto_publish_callback_set(mosq, my_publish_callback);

	mosquitto_connect(mosq, "127.0.0.1", 1885, 600, true);
	for(i=0; i<MESSAGE_COUNT; i++){
		mosquitto_publish(mosq, NULL, "perf/test", 100, "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789", 0, false);
	}

	while(!mosquitto_loop(mosq, 1) && run){
	}
	printf("Start: %ld\nStop: %ld\nDiff: %ld\nMessages/s: %f\n", start, stop, stop-start, (float)MESSAGE_COUNT/(float)(stop-start));

	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();

	return 0;
}
