/*
Copyright (c) 2010 Roger Light <roger@atchoo.org>
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

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <mqtt3.h>
#include <subs.h>
#include <memory_mosq.h>
#include <util_mosq.h>

struct _sub_token {
	struct _sub_token *next;
	char *topic;
};

static int _subs_process(struct _mosquitto_subhier *hier, const char *source_id, const char *topic, int qos, int retain, struct mosquitto_msg_store *stored)
{
	int rc = 0;
	int client_qos, msg_qos;
	uint16_t mid;
	struct _mosquitto_subleaf *leaf;

	leaf = hier->subs;

	if(retain){
		if(hier->retained){
			hier->retained->ref_count--;
			/* FIXME - it would be nice to be able to remove the message from the store at this point if ref_count == 0 */
		}
		if(stored->msg.payloadlen){
			hier->retained = stored;
			hier->retained->ref_count++;
		}else{
			hier->retained = NULL;
		}
	}
	while(leaf){
		if(leaf->context->bridge && !strcmp(leaf->context->core.id, source_id)){
			leaf = leaf->next;
			continue;
		}
		client_qos = leaf->qos;

		if(qos > client_qos){
			msg_qos = client_qos;
		}else{
			msg_qos = qos;
		}
		if(msg_qos){
			mid = _mosquitto_mid_generate(&leaf->context->core);
		}else{
			mid = 0;
		}
		switch(msg_qos){
			case 0:
				if(mqtt3_db_message_insert(leaf->context, mid, mosq_md_out, ms_publish, msg_qos, false, stored) == 1) rc = 1;
				break;
			case 1:
				if(mqtt3_db_message_insert(leaf->context, mid, mosq_md_out, ms_publish_puback, msg_qos, false, stored) == 1) rc = 1;
				break;
			case 2:
				if(mqtt3_db_message_insert(leaf->context, mid, mosq_md_out, ms_publish_pubrec, msg_qos, false, stored) == 1) rc = 1;
				break;
		}
		leaf = leaf->next;
	}
	return 0;
}

static int _sub_topic_tokenise(const char *subtopic, struct _sub_token **topics)
{
	struct _sub_token *new_topic, *tail = NULL;
	char *token;
	char *local_subtopic = NULL;

	assert(subtopic);
	assert(topics);

	local_subtopic = _mosquitto_strdup(subtopic);
	if(!local_subtopic) return 1;

	token = strtok(local_subtopic, "/");
	while(token){
		new_topic = _mosquitto_malloc(sizeof(struct _sub_token));
		if(!new_topic) goto cleanup;
		new_topic->next = NULL;
		new_topic->topic = _mosquitto_strdup(token);
		if(!new_topic->topic) goto cleanup;

		if(tail){
			tail->next = new_topic;
			tail = tail->next;
		}else{
			tail = new_topic;
			*topics = tail;
		}
		token = strtok(NULL, "/");
	}
	
	_mosquitto_free(local_subtopic);

	return 0;

cleanup:
	_mosquitto_free(local_subtopic);

	tail = *topics;
	*topics = NULL;
	while(tail){
		if(tail->topic) _mosquitto_free(tail->topic);
		new_topic = tail->next;
		_mosquitto_free(tail);
		tail = new_topic;
	}
	return 1;
}

static int _sub_add(mqtt3_context *context, int qos, struct _mosquitto_subhier *subhier, struct _sub_token *tokens)
{
	struct _mosquitto_subhier *branch, *last = NULL;
	struct _mosquitto_subleaf *leaf, *last_leaf;

	if(!tokens){
		if(context){
			leaf = subhier->subs;
			last_leaf = NULL;
			while(leaf){
				last_leaf = leaf;
				leaf = leaf->next;
			}
			leaf = _mosquitto_malloc(sizeof(struct _mosquitto_subleaf));
			if(!leaf) return 1;
			leaf->next = NULL;
			leaf->context = context;
			leaf->qos = qos;
			if(last_leaf){
				last_leaf->next = leaf;
				leaf->prev = last_leaf;
			}else{
				subhier->subs = leaf;
				leaf->prev = NULL;
			}
		}
		return 0;
	}

	branch = subhier->children;
	while(branch){
		if(!strcmp(branch->topic, tokens->topic)){
			return _sub_add(context, qos, branch, tokens->next);
		}
		last = branch;
		branch = branch->next;
	}
	/* Not found */
	branch = _mosquitto_calloc(1, sizeof(struct _mosquitto_subhier));
	if(!branch) return 1;
	if(!last){
		subhier->children = branch;
	}else{
		last->next = branch;
	}
	branch->topic = _mosquitto_strdup(tokens->topic);
	return _sub_add(context, qos, branch, tokens->next);
}

static int _sub_remove(mqtt3_context *context, struct _mosquitto_subhier *subhier, struct _sub_token *tokens)
{
	struct _mosquitto_subhier *branch, *last = NULL;
	struct _mosquitto_subleaf *leaf;

	if(!tokens){
		leaf = subhier->subs;
		while(leaf){
			if(leaf->context==context){
				if(leaf->prev){
					leaf->prev->next = leaf->next;
				}else{
					subhier->subs = leaf->next;
				}
				if(leaf->next){
					leaf->next->prev = leaf->prev;
				}
				_mosquitto_free(leaf);
				return 0;
			}
			leaf = leaf->next;
		}
		return 0;
	}

	branch = subhier->children;
	while(branch){
		if(!strcmp(branch->topic, tokens->topic)){
			_sub_remove(context, branch, tokens->next);
			if(!branch->children && !branch->subs && !branch->retained){
				if(last){
					last->next = branch->next;
				}else{
					subhier->children = branch->next;
				}
				_mosquitto_free(branch->topic);
				_mosquitto_free(branch);
			}
			return 0;
		}
		last = branch;
		branch = branch->next;
	}
	return 0;
}

static int _sub_search(struct _mosquitto_subhier *subhier, struct _sub_token *tokens, const char *source_id, const char *topic, int qos, int retain, struct mosquitto_msg_store *stored)
{
	/* FIXME - need to take into account source_id if the client is a bridge */
	struct _mosquitto_subhier *branch, *last = NULL;

	branch = subhier->children;
	while(branch){
		if(!strcmp(branch->topic, tokens->topic) || !strcmp(branch->topic, "+")){
			/* The topic matches this subscription.
			 * Doesn't include # wildcards */
			if(tokens->next){
				_sub_search(branch, tokens->next, source_id, topic, qos, retain, stored);
			}else{
				_subs_process(branch, source_id, topic, qos, retain, stored);
			}
		}else if(!strcmp(branch->topic, "#") && !branch->children){
			/* The topic matches due to a # wildcard - process the
			 * subscriptions and return. */
			_subs_process(branch, source_id, topic, qos, retain, stored);
			break;
		}
		last = branch;
		branch = branch->next;
	}
	return 0;
}

int mqtt3_sub_add(mqtt3_context *context, const char *sub, int qos, struct _mosquitto_subhier *root)
{
	int tree;
	int rc = 0;
	struct _mosquitto_subhier *subhier;
	struct _sub_token *tokens = NULL, *tail;

	assert(root);
	assert(sub);

	if(!strncmp(sub, "$SYS/", 5)){
		tree = 2;
		if(strlen(sub+5) == 0) return 0;
		if(_sub_topic_tokenise(sub+5, &tokens)) return 1;
	}else if(sub[0] == '/'){
		tree = 1;
		if(strlen(sub+1) == 0) return 0;
		if(_sub_topic_tokenise(sub+1, &tokens)) return 1;
	}else{
		tree = 0;
		if(strlen(sub) == 0) return 0;
		if(_sub_topic_tokenise(sub, &tokens)) return 1;
	}

	subhier = root->children;
	while(subhier){
		if(!strcmp(subhier->topic, "") && tree == 0){
			rc = _sub_add(context, qos, subhier, tokens);
			break;
		}else if(!strcmp(subhier->topic, "/") && tree == 1){
			rc = _sub_add(context, qos, subhier, tokens);
			break;
		}else if(!strcmp(subhier->topic, "$SYS") && tree == 2){
			rc = _sub_add(context, qos, subhier, tokens);
			break;
		}
		subhier = subhier->next;
	}

	while(tokens){
		tail = tokens->next;
		_mosquitto_free(tokens->topic);
		_mosquitto_free(tokens);
		tokens = tail;
	}
	return rc;
}

int mqtt3_sub_remove(mqtt3_context *context, const char *sub, struct _mosquitto_subhier *root)
{
	int rc = 0;
	int tree;
	struct _mosquitto_subhier *subhier;
	struct _sub_token *tokens = NULL, *tail;

	assert(root);
	assert(sub);

	if(!strncmp(sub, "$SYS/", 5)){
		tree = 2;
		if(_sub_topic_tokenise(sub+5, &tokens)) return 1;
	}else if(sub[0] == '/'){
		tree = 1;
		if(_sub_topic_tokenise(sub, &tokens)) return 1;
	}else{
		tree = 0;
		if(_sub_topic_tokenise(sub, &tokens)) return 1;
	}

	subhier = root->children;
	while(subhier){
		if(!strcmp(subhier->topic, "") && tree == 0){
			rc = _sub_remove(context, subhier, tokens);
			break;
		}else if(!strcmp(subhier->topic, "/") && tree == 1){
			rc = _sub_remove(context, subhier, tokens);
			break;
		}else if(!strcmp(subhier->topic, "$SYS") && tree == 2){
			rc = _sub_remove(context, subhier, tokens);
			break;
		}
		subhier = subhier->next;
	}

	while(tokens){
		tail = tokens->next;
		_mosquitto_free(tokens->topic);
		_mosquitto_free(tokens);
		tokens = tail;
	}

	return rc;
}

int mqtt3_sub_search(struct _mosquitto_subhier *root, const char *source_id, const char *topic, int qos, int retain, struct mosquitto_msg_store *stored)
{
	int rc = 0;
	int tree;
	struct _mosquitto_subhier *subhier;
	struct _sub_token *tokens = NULL, *tail;

	assert(root);
	assert(topic);

	if(!strncmp(topic, "$SYS/", 5)){
		tree = 2;
		if(_sub_topic_tokenise(topic+5, &tokens)) return 1;
	}else if(topic[0] == '/'){
		tree = 1;
		if(_sub_topic_tokenise(topic+1, &tokens)) return 1;
	}else{
		tree = 0;
		if(_sub_topic_tokenise(topic, &tokens)) return 1;
	}

	subhier = root->children;
	while(subhier){
		if(!strcmp(subhier->topic, "") && tree == 0){
			if(retain){
				/* We have a message that needs to be retained, so ensure that the subscription
				 * tree for its topic exists.
				 */
				_sub_add(NULL, 0, subhier, tokens);
			}
			rc = _sub_search(subhier, tokens, source_id, topic, qos, retain, stored);
		}else if(!strcmp(subhier->topic, "/") && tree == 1){
			if(retain){
				/* We have a message that needs to be retained, so ensure that the subscription
				 * tree for its topic exists.
				 */
				_sub_add(NULL, 0, subhier, tokens);
			}
			rc = _sub_search(subhier, tokens, source_id, topic, qos, retain, stored);
		}else if(!strcmp(subhier->topic, "$SYS") && tree == 2){
			if(retain){
				/* We have a message that needs to be retained, so ensure that the subscription
				 * tree for its topic exists.
				 */
				_sub_add(NULL, 0, subhier, tokens);
			}
			rc = _sub_search(subhier, tokens, source_id, topic, qos, retain, stored);
		}
		subhier = subhier->next;
	}
	while(tokens){
		tail = tokens->next;
		_mosquitto_free(tokens->topic);
		_mosquitto_free(tokens);
		tokens = tail;
	}

	return rc;
}

static int _subs_clean_session(mqtt3_context *context, struct _mosquitto_subhier *root)
{
	int rc = 0;
	struct _mosquitto_subhier *child, *last = NULL;
	struct _mosquitto_subleaf *leaf, *next;

	if(!root) return 0;

	leaf = root->subs;
	while(leaf){
		if(leaf->context == context){
			if(leaf->prev){
				leaf->prev->next = leaf->next;
			}else{
				root->subs = leaf->next;
			}
			if(leaf->next){
				leaf->next->prev = leaf->prev;
			}
			next = leaf->next;
			_mosquitto_free(leaf);
			leaf = next;
		}else{
			leaf = leaf->next;
		}
	}

	child = root->children;
	while(child){
		_subs_clean_session(context, child);
		if(!child->children && !child->subs){
			if(last){
				last->next = child->next;
			}else{
				root->children = child->next;
			}
			_mosquitto_free(child->topic);
			_mosquitto_free(child);
			if(last){
				child = last->next;
			}else{
				child = root->children;
			}
		}else{
			last = child;
			child = child->next;
		}
	}
	return rc;
}

/* Remove all subscriptions for a client.
 */
int mqtt3_subs_clean_session(mqtt3_context *context, struct _mosquitto_subhier *root)
{
	struct _mosquitto_subhier *child;

	child = root->children;
	while(child){
		_subs_clean_session(context, child);
		child = child->next;
	}

	return 0;
}

void mqtt3_sub_tree_print(struct _mosquitto_subhier *root, int level)
{
	int i;
	struct _mosquitto_subhier *branch;
	struct _mosquitto_subleaf *leaf;

	for(i=0; i<level*2; i++){
		printf(" ");
	}
	printf("%s", root->topic);
	leaf = root->subs;
	while(leaf){
		printf(" (%s, %d)", "", leaf->qos);
		leaf = leaf->next;
	}
	if(root->retained){
		printf(" (r)");
	}
	printf("\n");

	branch = root->children;
	while(branch){
		mqtt3_sub_tree_print(branch, level+1);
		branch = branch->next;
	}
}

static int _retain_process(struct mosquitto_msg_store *retained, mqtt3_context *context, const char *sub, int sub_qos)
{
	int rc = 0;
	char *topic;
	int qos;
	uint16_t mid;

	topic = retained->msg.topic;
	qos = retained->msg.qos;

	if(qos > sub_qos) qos = sub_qos;
	if(qos > 0){
		mid = _mosquitto_mid_generate(&context->core);
	}else{
		mid = 0;
	}
	switch(qos){
		case 0:
			if(mqtt3_db_message_insert(context, mid, mosq_md_out, ms_publish, qos, true, retained) == 1) rc = 1;
			break;
		case 1:
			if(mqtt3_db_message_insert(context, mid, mosq_md_out, ms_publish_puback, qos, true, retained) == 1) rc = 1;
			break;
		case 2:
			if(mqtt3_db_message_insert(context, mid, mosq_md_out, ms_publish_pubrec, qos, true, retained) == 1) rc = 1;
			break;
	}
	return rc;
}

static int _retain_search(struct _mosquitto_subhier *subhier, struct _sub_token *tokens, mqtt3_context *context, const char *sub, int sub_qos)
{
	struct _mosquitto_subhier *branch, *last = NULL;

	branch = subhier->children;
	while(branch){
		/* Subscriptions with wildcards in aren't really valid topics to publish to
		 * so they can't have retained messages.
		 */
		if(strcmp(branch->topic, "+") && strcmp(branch->topic, "#")){
			if(!strcmp(tokens->topic, "#") && !tokens->next){
				if(branch->retained){
					_retain_process(branch->retained, context, sub, sub_qos);
				}
				_retain_search(branch, tokens, context, sub, sub_qos);
			}else if(!strcmp(branch->topic, tokens->topic) || !strcmp(branch->topic, "+")){
				if(tokens->next){
					_retain_search(branch, tokens->next, context, sub, sub_qos);
				}else{
					if(branch->retained){
						_retain_process(branch->retained, context, sub, sub_qos);
					}
				}
			}
		}
		last = branch;
		branch = branch->next;
	}
	return 0;
}

int mqtt3_retain_queue(mosquitto_db *db, mqtt3_context *context, const char *sub, int sub_qos)
{
	int rc = 0;
	int tree;
	struct _mosquitto_subhier *subhier;
	struct _sub_token *tokens = NULL, *tail;

	assert(db);
	assert(context);
	assert(sub);

	if(!strncmp(sub, "$SYS/", 5)){
		tree = 2;
		if(_sub_topic_tokenise(sub+5, &tokens)) return 1;
	}else if(sub[0] == '/'){
		tree = 1;
		if(_sub_topic_tokenise(sub+1, &tokens)) return 1;
	}else{
		tree = 0;
		if(_sub_topic_tokenise(sub, &tokens)) return 1;
	}

	subhier = db->subs.children;
	while(subhier){
		if(!strcmp(subhier->topic, "") && tree == 0){
			rc = _retain_search(subhier, tokens, context, sub, sub_qos);
			break;
		}else if(!strcmp(subhier->topic, "/") && tree == 1){
			rc = _retain_search(subhier, tokens, context, sub, sub_qos);
			break;
		}else if(!strcmp(subhier->topic, "$SYS") && tree == 2){
			rc = _retain_search(subhier, tokens, context, sub, sub_qos);
			break;
		}
		subhier = subhier->next;
	}
	while(tokens){
		tail = tokens->next;
		_mosquitto_free(tokens->topic);
		_mosquitto_free(tokens);
		tokens = tail;
	}

	return rc;
}

