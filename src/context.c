#include <stdlib.h>

#include <mqtt3.h>

mqtt3_context *mqtt3_context_init(int sock)
{
	mqtt3_context *context;

	context = mqtt3_malloc(sizeof(mqtt3_context));
	if(!context) return NULL;
	
	context->next = NULL;
	context->sock = sock;
	context->last_msg_in = time(NULL);
	context->last_msg_out = time(NULL);
	context->keepalive = 60; /* Default to 60s */
	context->clean_start = true;
	context->last_mid = 0;
	context->id = NULL;
	context->messages = NULL;

	return context;
}

void mqtt3_context_cleanup(mqtt3_context *context)
{
	if(!context) return;

	if(context->sock != -1){
		mqtt3_socket_close(context);
	}
	if(context->clean_start){
		mqtt3_db_subs_clean_start(context);
		mqtt3_db_client_delete(context);
	}
	if(context->id) mqtt3_free(context->id);
	/* FIXME - clean messages and subscriptions */
	mqtt3_free(context);
}


