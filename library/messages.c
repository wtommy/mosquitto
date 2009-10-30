#include <stdlib.h>

#include <mqtt3.h>

int mqtt_add_message(mqtt_context *context, mqtt_message *message)
{
	mqtt_message *pointer;
	if(!context || !message) return 0;

	message->next = NULL;

	if(context->messages){
		pointer = context->messages;
		while(pointer->next){
			pointer = pointer->next;
		}
		pointer->next = message;
	}else{
		context->messages = message;
	}

	return 1;
}

int mqtt_remove_message(mqtt_context *context, uint16_t mid)
{
	mqtt_message *pointer, *prev;

	if(!context || !(context->messages)) return 0;
	prev = NULL;
	pointer = context->messages;

	while(pointer){
		if(pointer->message_id != mid){
			prev = pointer;
			pointer = pointer->next;
		}else{
			if(prev){
				prev->next = pointer->next;
			}else{
				context->messages = pointer->next;
			}
			mqtt_cleanup_message(pointer);
			break;
		}
	}

	return 1;
}

void mqtt_cleanup_message(mqtt_message *message)
{
	if(!message) return;
	if(message->variable_header) free(message->variable_header);
	if(message->payload) free(message->payload);
	free(message);
}

void mqtt_cleanup_messages(mqtt_context *context)
{
	mqtt_message *pointer, *next;

	if(!context || !(context->messages)) return;

	pointer = context->messages;
	while(pointer){
		next = pointer->next;
		mqtt_cleanup_message(pointer);
		pointer = next;
	}
}

