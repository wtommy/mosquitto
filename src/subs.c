/*
Copyright (c) 2010,2011 Roger Light <roger@atchoo.org>
Copyright (c) 2011 Sang Kyeong Nam
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

#include <config.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <mqtt3.h>
#include <memory_mosq.h>
#include <util_mosq.h>

struct _sub_token {
	struct _sub_token *next;
	char *topic;
};

static int _subs_process(struct _mosquitto_db *db, struct _mosquitto_subhier *hier, const char *source_id, const char *topic, int qos, int retain, struct mosquitto_msg_store *stored)
{
	int rc = 0;
	int rc2;
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
	while(source_id && leaf){
		if(leaf->context->bridge && !strcmp(leaf->context->id, source_id)){
			leaf = leaf->next;
			continue;
		}
		/* Check for ACL topic access. */
		rc2 = mosquitto_acl_check(db, leaf->context, topic, MOSQ_ACL_READ);
		if(rc2 == MOSQ_ERR_ACL_DENIED){
			leaf = leaf->next;
			continue;
		}else if(rc2 == MOSQ_ERR_SUCCESS){
			client_qos = leaf->qos;

			if(qos > client_qos){
				msg_qos = client_qos;
			}else{
				msg_qos = qos;
			}
			if(msg_qos){
				mid = _mosquitto_mid_generate(leaf->context);
			}else{
				mid = 0;
			}
			if(mqtt3_db_message_insert(db, leaf->context, mid, mosq_md_out, msg_qos, false, stored) == 1) rc = 1;
		}else{
			rc = 1;
		}
		leaf = leaf->next;
	}
	return rc;
}

static int _sub_topic_tokenise(const char *subtopic, struct _sub_token **topics)
{
	struct _sub_token *new_topic, *tail = NULL;
	char *token;
	char *local_subtopic = NULL;
	char *real_subtopic;

	assert(subtopic);
	assert(topics);

	local_subtopic = _mosquitto_strdup(subtopic);
	if(!local_subtopic) return MOSQ_ERR_NOMEM;
	real_subtopic = local_subtopic;

	if(local_subtopic[0] == '/'){
		new_topic = _mosquitto_malloc(sizeof(struct _sub_token));
		if(!new_topic) goto cleanup;
		new_topic->next = NULL;
		new_topic->topic = _mosquitto_strdup("/");
		if(!new_topic->topic) goto cleanup;

		*topics = new_topic;
		tail = new_topic;

		local_subtopic++;
	}

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
	
	_mosquitto_free(real_subtopic);

	return MOSQ_ERR_SUCCESS;

cleanup:
	_mosquitto_free(real_subtopic);

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

static int _sub_add(struct mosquitto *context, int qos, struct _mosquitto_subhier *subhier, struct _sub_token *tokens)
{
	struct _mosquitto_subhier *branch, *last = NULL;
	struct _mosquitto_subleaf *leaf, *last_leaf;

	if(!tokens){
		if(context){
			leaf = subhier->subs;
			last_leaf = NULL;
			while(leaf){
				if(!strcmp(leaf->context->id, context->id)){
					/* Client making a second subscription to same topic. Only
					 * need to update QoS. Return -1 to indicate this to the
					 * calling function. */
					leaf->qos = qos;
					return -1;
				}
				last_leaf = leaf;
				leaf = leaf->next;
			}
			leaf = _mosquitto_malloc(sizeof(struct _mosquitto_subleaf));
			if(!leaf) return MOSQ_ERR_NOMEM;
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
		return MOSQ_ERR_SUCCESS;
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
	if(!branch) return MOSQ_ERR_NOMEM;
	branch->topic = _mosquitto_strdup(tokens->topic);
	if(!branch->topic) return MOSQ_ERR_NOMEM;
	if(!last){
		subhier->children = branch;
	}else{
		last->next = branch;
	}
	return _sub_add(context, qos, branch, tokens->next);
}

static int _sub_remove(struct mosquitto *context, struct _mosquitto_subhier *subhier, struct _sub_token *tokens)
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
				return MOSQ_ERR_SUCCESS;
			}
			leaf = leaf->next;
		}
		return MOSQ_ERR_SUCCESS;
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
			return MOSQ_ERR_SUCCESS;
		}
		last = branch;
		branch = branch->next;
	}
	return MOSQ_ERR_SUCCESS;
}

static int _sub_search(struct _mosquitto_db *db, struct _mosquitto_subhier *subhier, struct _sub_token *tokens, const char *source_id, const char *topic, int qos, int retain, struct mosquitto_msg_store *stored)
{
	/* FIXME - need to take into account source_id if the client is a bridge */
	struct _mosquitto_subhier *branch;
	int flag = 0;

	branch = subhier->children;
	while(branch){
		if(tokens && tokens->topic && (!strcmp(branch->topic, tokens->topic) || !strcmp(branch->topic, "+"))){
			/* The topic matches this subscription.
			 * Doesn't include # wildcards */
			_sub_search(db, branch, tokens->next, source_id, topic, qos, retain, stored);
			if(!tokens->next){
				_subs_process(db, branch, source_id, topic, qos, retain, stored);
			}
		}else if(!strcmp(branch->topic, "#") && !branch->children && (!tokens || strcmp(tokens->topic, "/"))){
			/* The topic matches due to a # wildcard - process the
			 * subscriptions but *don't* return. Although this branch has ended
			 * there may still be other subscriptions to deal with.
			 */
			_subs_process(db, branch, source_id, topic, qos, retain, stored);
			flag = -1;
		}
		branch = branch->next;
	}
	return flag;
}

int mqtt3_sub_add(struct mosquitto *context, const char *sub, int qos, struct _mosquitto_subhier *root)
{
	int tree;
	int rc = 0;
	struct _mosquitto_subhier *subhier;
	struct _sub_token *tokens = NULL, *tail;

	assert(root);
	assert(sub);

	if(!strncmp(sub, "$SYS/", 5)){
		tree = 2;
		if(strlen(sub+5) == 0) return MOSQ_ERR_SUCCESS;
		if(_sub_topic_tokenise(sub+5, &tokens)) return 1;
	}else{
		tree = 0;
		if(strlen(sub) == 0) return MOSQ_ERR_SUCCESS;
		if(_sub_topic_tokenise(sub, &tokens)) return 1;
	}

	subhier = root->children;
	while(subhier){
		if(!strcmp(subhier->topic, "") && tree == 0){
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
	/* We aren't worried about -1 (already subscribed) return codes. */
	if(rc == -1) rc = MOSQ_ERR_SUCCESS;
	return rc;
}

int mqtt3_sub_remove(struct mosquitto *context, const char *sub, struct _mosquitto_subhier *root)
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
	}else{
		tree = 0;
		if(_sub_topic_tokenise(sub, &tokens)) return 1;
	}

	subhier = root->children;
	while(subhier){
		if(!strcmp(subhier->topic, "") && tree == 0){
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

int mqtt3_db_messages_queue(struct _mosquitto_db *db, const char *source_id, const char *topic, int qos, int retain, struct mosquitto_msg_store *stored)
{
	int rc = 0;
	int tree;
	struct _mosquitto_subhier *subhier;
	struct _sub_token *tokens = NULL, *tail;

	assert(db);
	assert(topic);

	if(!strncmp(topic, "$SYS/", 5)){
		tree = 2;
		if(_sub_topic_tokenise(topic+5, &tokens)) return 1;
	}else{
		tree = 0;
		if(_sub_topic_tokenise(topic, &tokens)) return 1;
	}

	subhier = db->subs.children;
	while(subhier){
		if(!strcmp(subhier->topic, "") && tree == 0){
			if(retain){
				/* We have a message that needs to be retained, so ensure that the subscription
				 * tree for its topic exists.
				 */
				_sub_add(NULL, 0, subhier, tokens);
			}
			rc = _sub_search(db, subhier, tokens, source_id, topic, qos, retain, stored);
			if(rc == -1){
				_subs_process(db, subhier, source_id, topic, qos, retain, stored);
				rc = 0;
			}
		}else if(!strcmp(subhier->topic, "$SYS") && tree == 2){
			if(retain){
				/* We have a message that needs to be retained, so ensure that the subscription
				 * tree for its topic exists.
				 */
				_sub_add(NULL, 0, subhier, tokens);
			}
			rc = _sub_search(db, subhier, tokens, source_id, topic, qos, retain, stored);
			if(rc == -1){
				_subs_process(db, subhier, source_id, topic, qos, retain, stored);
				rc = 0;
			}
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

static int _subs_clean_session(struct mosquitto *context, struct _mosquitto_subhier *root)
{
	int rc = 0;
	struct _mosquitto_subhier *child, *last = NULL;
	struct _mosquitto_subleaf *leaf, *next;

	if(!root) return MOSQ_ERR_SUCCESS;

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
		if(!child->children && !child->subs && !child->retained){
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
int mqtt3_subs_clean_session(struct mosquitto *context, struct _mosquitto_subhier *root)
{
	struct _mosquitto_subhier *child;

	child = root->children;
	while(child){
		_subs_clean_session(context, child);
		child = child->next;
	}

	return MOSQ_ERR_SUCCESS;
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

static int _retain_process(struct _mosquitto_db *db, struct mosquitto_msg_store *retained, struct mosquitto *context, const char *sub, int sub_qos)
{
	int rc = 0;
	int qos;
	uint16_t mid;

	rc = mosquitto_acl_check(db, context, retained->msg.topic, MOSQ_ACL_READ);
	if(rc == MOSQ_ERR_ACL_DENIED){
		return MOSQ_ERR_SUCCESS;
	}else if(rc != MOSQ_ERR_SUCCESS){
		return rc;
	}

	qos = retained->msg.qos;

	if(qos > sub_qos) qos = sub_qos;
	if(qos > 0){
		mid = _mosquitto_mid_generate(context);
	}else{
		mid = 0;
	}
	return mqtt3_db_message_insert(db, context, mid, mosq_md_out, qos, true, retained);
}

static int _retain_search(struct _mosquitto_db *db, struct _mosquitto_subhier *subhier, struct _sub_token *tokens, struct mosquitto *context, const char *sub, int sub_qos)
{
	struct _mosquitto_subhier *branch;

	branch = subhier->children;
	while(branch){
		/* Subscriptions with wildcards in aren't really valid topics to publish to
		 * so they can't have retained messages.
		 */
		if(_mosquitto_topic_wildcard_len_check(branch->topic) == MOSQ_ERR_SUCCESS){
			if(!strcmp(tokens->topic, "#") && !tokens->next){
				if(branch->retained){
					_retain_process(db, branch->retained, context, sub, sub_qos);
				}
				_retain_search(db, branch, tokens, context, sub, sub_qos);
			}else if(!strcmp(branch->topic, tokens->topic) || !strcmp(tokens->topic, "+")){
				if(tokens->next){
					_retain_search(db, branch, tokens->next, context, sub, sub_qos);
				}else{
					if(branch->retained){
						_retain_process(db, branch->retained, context, sub, sub_qos);
					}
				}
			}
		}
		branch = branch->next;
	}
	return MOSQ_ERR_SUCCESS;
}

int mqtt3_retain_queue(mosquitto_db *db, struct mosquitto *context, const char *sub, int sub_qos)
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
	}else{
		tree = 0;
		if(_sub_topic_tokenise(sub, &tokens)) return 1;
	}

	subhier = db->subs.children;
	while(subhier){
		if(!strcmp(subhier->topic, "") && tree == 0){
			rc = _retain_search(db, subhier, tokens, context, sub, sub_qos);
			break;
		}else if(!strcmp(subhier->topic, "$SYS") && tree == 2){
			rc = _retain_search(db, subhier, tokens, context, sub, sub_qos);
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

