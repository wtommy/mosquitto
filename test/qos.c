#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mosquitto.h>

struct msg_list{
	struct msg_list *next;
	struct mosquitto_message msg;
};

struct msg_list *messages_received = NULL;

void on_message(void *obj, const struct mosquitto_message *msg)
{
	struct msg_list *tail, *new_list;

	new_list = malloc(sizeof(struct msg_list));
	if(!new_list){
		fprintf(stderr, "Error allocating list memory.\n");
		return;
	}
	new_list->next = NULL;
	if(!mosquitto_message_copy(&new_list->msg, msg)){
		if(messages_received){
			tail = messages_received;
			while(tail->next){
				tail = tail->next;
			}
			tail->next = new_list;
		}else{
			messages_received = new_list;
		}
	}else{
		free(new_list);
		return;
	}
}

void rand_publish(struct mosquitto *mosq, const char *topic, int qos)
{
}

int main(int argc, char *argv[])
{
	struct mosquitto *mosq;
	int i;

	mosquitto_lib_init();

	mosq = mosquitto_new("qos-test", NULL);
	mosquitto_message_callback_set(mosq, on_message);

	mosquitto_connect(mosq, "127.0.0.1", 1883, 60, true);
	mosquitto_subscribe(mosq, NULL, "qos-test/0", 0);
	mosquitto_subscribe(mosq, NULL, "qos-test/1", 1);
	mosquitto_subscribe(mosq, NULL, "qos-test/2", 2);

	for(i=0; i<100; i++){
		rand_publish(mosq, "qos-test/0", 0);
		rand_publish(mosq, "qos-test/0", 1);
		rand_publish(mosq, "qos-test/0", 2);
		rand_publish(mosq, "qos-test/1", 0);
		rand_publish(mosq, "qos-test/1", 1);
		rand_publish(mosq, "qos-test/1", 2);
		rand_publish(mosq, "qos-test/2", 0);
		rand_publish(mosq, "qos-test/2", 1);
		rand_publish(mosq, "qos-test/2", 2);
	}
	while(!mosquitto_loop(mosq, -1)){
	}

	mosquitto_destroy(mosq);

	mosquitto_lib_cleanup();

	return 0;
}

