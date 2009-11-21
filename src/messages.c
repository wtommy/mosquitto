#include <stdlib.h>

#include <mqtt3.h>

int mqtt3_message_add(mqtt3_context *context, mqtt3_message *message)
{
	mqtt3_message *pointer;
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

int mqtt3_message_remove(mqtt3_context *context, uint16_t mid)
{
	mqtt3_message *pointer, *prev;

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
			mqtt3_message_cleanup(pointer);
			break;
		}
	}

	return 1;
}

void mqtt3_message_cleanup(mqtt3_message *message)
{
	if(!message) return;
	if(message->variable_header) free(message->variable_header);
	if(message->payload) free(message->payload);
	free(message);
}

void mqtt3_messages_cleanup(mqtt3_context *context)
{
	mqtt3_message *pointer, *next;

	if(!context || !(context->messages)) return;

	pointer = context->messages;
	while(pointer){
		next = pointer->next;
		mqtt3_message_cleanup(pointer);
		pointer = next;
	}
}

