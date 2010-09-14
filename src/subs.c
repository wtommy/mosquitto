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
#include <string.h>
#include <mqtt3.h>
#include <subs.h>
#include <memory_mosq.h>

static int _sub_add(mqtt3_context *context, int qos, struct _mosquitto_subhier *subhier, char *sub)
{
	char *token;
	struct _mosquitto_subhier *branch, *last = NULL;
	struct _mosquitto_subleaf *leaf, *last_leaf;

	if(!sub){
		token = strtok(NULL, "/");
	}else{
		token = strtok(sub, "/");
	}
	if(!token){
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
		leaf->client_id = _mosquitto_strdup(context->core.id);
		if(!leaf->client_id){
			_mosquitto_free(leaf);
			return 1;
		}
		leaf->qos = qos;
		if(last_leaf){
			last_leaf->next = leaf;
		}else{
			subhier->subs = leaf;
		}
		return 0;
	}

	branch = subhier->children;
	while(branch){
		if(!strcmp(branch->topic, token)){
			return _sub_add(context, qos, branch, NULL);
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
	branch->topic = _mosquitto_strdup(token);
	return _sub_add(context, qos, branch, NULL);
}

static int _sub_remove(mqtt3_context *context, struct _mosquitto_subhier *subhier, char *sub)
{
	char *token;
	struct _mosquitto_subhier *branch, *last = NULL;
	struct _mosquitto_subleaf *leaf, *last_leaf;

	if(!sub){
		token = strtok(NULL, "/");
	}else{
		token = strtok(sub, "/");
	}
	if(!token){
		leaf = subhier->subs;
		last_leaf = NULL;
		while(leaf){
			if(leaf->context==context){
				if(last_leaf){
					last_leaf->next = leaf->next;
				}else{
					subhier->subs = leaf->next;
				}
				_mosquitto_free(leaf->client_id);
				_mosquitto_free(leaf);
				return 0;
			}
			last_leaf = leaf;
			leaf = leaf->next;
		}
		return 0;
	}

	branch = subhier->children;
	while(branch){
		if(!strcmp(branch->topic, token)){
			_sub_remove(context, branch, NULL);
			if(!branch->children && !branch->subs){
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

static int _sub_search(struct _mosquitto_subhier *subhier, char *topic)
{
	char *token;
	struct _mosquitto_subhier *branch, *last = NULL;
	struct _mosquitto_subleaf *leaf, *last_leaf;

	if(!topic){
		token = strtok(NULL, "/");
	}else{
		token = strtok(topic, "/");
	}
	if(!token){
		leaf = subhier->subs;
		last_leaf = NULL;
		while(leaf){
			/* FIXME - this is subscribed, send message */
			leaf = leaf->next;
		}
		return 0;
	}

	branch = subhier->children;
	while(branch){
		if(!strcmp(branch->topic, token) || !strcmp(branch->topic, "+")){
			/* The topic matches this subscription.
			 * Doesn't include # wildcards */
			_sub_search(branch, NULL);
		}else if(!strcmp(branch->topic, "#") && !branch->children){
			/* The topic matches due to a # wildcard - process the
			 * subscriptions and return. */
			/* FIXME */
			return 0;
		}
		last = branch;
		branch = branch->next;
	}
	return 0;
}

int mqtt3_sub_add(mqtt3_context *context, int qos, struct _mosquitto_subhier *root, const char *sub)
{
	char *local_sub;
	int tree;
	struct _mosquitto_subhier *subhier;
	int rc;

	assert(root);
	assert(sub);

	if(!strncmp(sub, "$SYS", 4)){
		tree = 2;
		local_sub = _mosquitto_strdup(sub);
	}else if(sub[0] == '/'){
		tree = 1;
		local_sub = _mosquitto_strdup(sub+1);
	}else{
		tree = 0;
		local_sub = _mosquitto_strdup(sub);
	}
	if(!local_sub) return 1;

	subhier = root->children;
	while(subhier){
		if(!strcmp(subhier->topic, "") && tree == 0){
			rc = _sub_add(context, qos, subhier, local_sub);
			_mosquitto_free(local_sub);
			return rc;
		}else if(!strcmp(subhier->topic, "/") && tree == 1){
			rc = _sub_add(context, qos, subhier, local_sub);
			_mosquitto_free(local_sub);
			return rc;
		}else if(!strcmp(subhier->topic, "$SYS") && tree == 2){
			rc = _sub_add(context, qos, subhier, local_sub);
			_mosquitto_free(local_sub);
			return rc;
		}
		subhier = subhier->next;
	}

	_mosquitto_free(local_sub);
	return 1;
}

int mqtt3_sub_remove(mqtt3_context *context, struct _mosquitto_subhier *root, const char *sub)
{
	char *local_sub;
	int tree;
	struct _mosquitto_subhier *subhier;
	int rc;

	assert(root);
	assert(sub);

	if(!strncmp(sub, "$SYS", 4)){
		tree = 2;
		local_sub = _mosquitto_strdup(sub);
	}else if(sub[0] == '/'){
		tree = 1;
		local_sub = _mosquitto_strdup(sub+1);
	}else{
		tree = 0;
		local_sub = _mosquitto_strdup(sub);
	}
	if(!local_sub) return 1;

	subhier = root->children;
	while(subhier){
		if(!strcmp(subhier->topic, "") && tree == 0){
			rc = _sub_remove(context, subhier, local_sub);
			_mosquitto_free(local_sub);
			return rc;
		}else if(!strcmp(subhier->topic, "/") && tree == 1){
			rc = _sub_remove(context, subhier, local_sub);
			_mosquitto_free(local_sub);
			return rc;
		}else if(!strcmp(subhier->topic, "$SYS") && tree == 2){
			rc = _sub_remove(context, subhier, local_sub);
			_mosquitto_free(local_sub);
			return rc;
		}
		subhier = subhier->next;
	}

	_mosquitto_free(local_sub);
	return 1;
}

int mqtt3_sub_search(struct _mosquitto_subhier *root, const char *topic)
{
	char *stmp;
	int hier;
	char *local_topic;
	int tree;
	struct _mosquitto_subhier *subhier;

	assert(root);
	assert(topic);

	if(!strncmp(topic, "$SYS", 4)){
		tree = 2;
		local_topic = _mosquitto_strdup(topic);
	}else if(topic[0] == '/'){
		tree = 1;
		local_topic = _mosquitto_strdup(topic+1);
	}else{
		tree = 0;
		local_topic = _mosquitto_strdup(topic);
	}
	if(!local_topic) return 1;

	hier = 0;
	stmp = local_topic;
	while(stmp){
		stmp = index(stmp, '/');
		if(stmp) stmp++;
		hier++;
	}

	subhier = root->children;
	while(subhier){
		if(!strcmp(subhier->topic, "") && tree == 0){
			if(_sub_search(subhier, local_topic)){
				_mosquitto_free(local_topic);
				return 1;
			}
		}else if(!strcmp(subhier->topic, "/") && tree == 1){
			if(_sub_search(subhier, local_topic)){
				_mosquitto_free(local_topic);
				return 1;
			}
		}else if(!strcmp(subhier->topic, "$SYS") && tree == 2){
			if(_sub_search(subhier, local_topic)){
				_mosquitto_free(local_topic);
				return 1;
			}
		}
		subhier = subhier->next;
	}
	_mosquitto_free(local_topic);
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
	printf("\n");

	branch = root->children;
	while(branch){
		mqtt3_sub_tree_print(branch, level+1);
		branch = branch->next;
	}
}

