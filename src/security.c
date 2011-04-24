/*
Copyright (c) 2011 Roger Light <roger@atchoo.org>
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

#include <stdio.h>
#include <string.h>

#include <memory_mosq.h>
#include <mqtt3.h>

int _add_acl(struct _mosquitto_db *db, const char *user, const char *topic, int access)
{
	struct _mosquitto_acl_user *acl_user=NULL, *user_tail;
	struct _mosquitto_acl *acl, *acl_root=NULL, *acl_tail=NULL;
	char *local_topic;
	char *token = NULL;
	bool new_user = false;

	if(!db || !topic) return MOSQ_ERR_INVAL;

	local_topic = _mosquitto_strdup(topic);
	if(!local_topic){
		return MOSQ_ERR_NOMEM;
	}

	if(db->acl_list){
		user_tail = db->acl_list;
		while(user_tail){
			if(user == NULL){
				if(user_tail->username == NULL){
					acl_user = user_tail;
					break;
				}
			}else if(user_tail->username && !strcmp(user_tail->username, user)){
				acl_user = user_tail;
				break;
			}
			user_tail = user_tail->next;
		}
	}
	if(!acl_user){
		acl_user = _mosquitto_malloc(sizeof(struct _mosquitto_acl_user));
		if(!acl_user){
			_mosquitto_free(local_topic);
			return MOSQ_ERR_NOMEM;
		}
		new_user = true;
		if(user){
			acl_user->username = _mosquitto_strdup(user);
			if(!acl_user->username){
				_mosquitto_free(local_topic);
				_mosquitto_free(acl_user);
				return MOSQ_ERR_NOMEM;
			}
		}else{
			acl_user->username = NULL;
		}
		acl_user->next = NULL;
	}

	/* Tokenise topic */
	if(local_topic[0] == '/'){
		acl_root = _mosquitto_malloc(sizeof(struct _mosquitto_acl));
		if(!acl_root) return MOSQ_ERR_NOMEM;
		acl_tail = acl_root;
		acl_root->child = NULL;
		acl_root->next = NULL;
		acl_root->access = MOSQ_ACL_NONE;
		acl_root->topic = _mosquitto_strdup("/");
		if(!acl_root->topic) return MOSQ_ERR_NOMEM;

		token = strtok(local_topic+1, "/");
	}else{
		token = strtok(local_topic, "/");
	}

	while(token){
		acl = _mosquitto_malloc(sizeof(struct _mosquitto_acl));
		if(!acl) return MOSQ_ERR_NOMEM;
		acl->child = NULL;
		acl->next = NULL;
		acl->access = MOSQ_ACL_NONE;
		acl->topic = _mosquitto_strdup(token);
		if(!acl->topic) return MOSQ_ERR_NOMEM;
		if(acl_root){
			acl_tail->child = acl;
			acl_tail = acl;
		}else{
			acl_root = acl;
			acl_tail = acl;
		}

		token = strtok(NULL, "/");
	}
	if(acl_root){
		acl_tail = acl_root;
		while(acl_tail->next){
			acl_tail = acl_tail->next;
		}
		acl_tail->access = access;
	}else{
		return MOSQ_ERR_INVAL;
	}

	/* Add acl to user acl list */
	if(acl_user->acl){
		acl_tail = acl_user->acl;
		while(acl_tail->next){
			acl_tail = acl_tail->next;
		}
		acl_tail->next = acl_root;
	}else{
		acl_user->acl = acl_root;
	}

	if(new_user){
		/* Add to end of list */
		if(db->acl_list){
			user_tail = db->acl_list;
			while(user_tail->next){
				user_tail = user_tail->next;
			}
			user_tail->next = acl_user;
		}else{
			db->acl_list = acl_user;
		}
	}

	_mosquitto_free(local_topic);
	return MOSQ_ERR_SUCCESS;
}

int mqtt3_aclfile_parse(struct _mosquitto_db *db)
{
	FILE *aclfile;
	char buf[1024];
	char *token;
	char *user = NULL;
	char *topic;
	char *access_s;
	int access;
	int rc;

	if(!db || !db->config) return MOSQ_ERR_INVAL;
	if(!db->config->acl_file) return MOSQ_ERR_SUCCESS;

	aclfile = fopen(db->config->acl_file, "rt");
	if(!aclfile) return 1;

	// topic [read|write] <topic> 
	// user <user>

	while(fgets(buf, 1024, aclfile)){
		token = strtok(buf, " ");
		if(token){
			if(!strcmp(token, "topic")){
				access_s = strtok(NULL, " ");
				if(!access_s){
					mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty topic in acl_file.");
					if(user) _mosquitto_free(user);
					fclose(aclfile);
					return 1;
				}
				token = strtok(NULL, " ");
				if(token){
					topic = token;
				}else{
					topic = access_s;
					access_s = NULL;
				}
				if(access_s){
					if(!strcmp(access_s, "read")){
						access = MOSQ_ACL_READ;
					}else if(!strcmp(access_s, "write")){
						access = MOSQ_ACL_WRITE;
					}else{
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty invalid topic access type in acl_file.");
						if(user) _mosquitto_free(user);
						fclose(aclfile);
						return 1;
					}
				}else{
					access = MOSQ_ACL_READWRITE;
				}
				rc = _add_acl(db, user, topic, access);
				if(rc) return rc;
			}else if(!strcmp(token, "user")){
				token = strtok(NULL, " ");
				if(token){
					if(user) _mosquitto_free(user);
					user = _mosquitto_strdup(token);
					if(!user){
						fclose(aclfile);
						return MOSQ_ERR_NOMEM;
					}
				}else{
					mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Missing username in acl_file.");
					if(user) _mosquitto_free(user);
					fclose(aclfile);
					return 1;
				}
			}
		}
	}

	fclose(aclfile);

	return MOSQ_ERR_SUCCESS;
}

int mqtt3_pwfile_parse(struct _mosquitto_db *db)
{
	FILE *pwfile;
	struct _mosquitto_unpwd *unpwd;
	char buf[256];
	char *username, *password;
	int len;

	if(!db || !db->config) return MOSQ_ERR_INVAL;

	if(!db->config->password_file) return MOSQ_ERR_SUCCESS;

	pwfile = fopen(db->config->password_file, "rt");
	if(!pwfile) return 1;

	while(!feof(pwfile)){
		if(fgets(buf, 256, pwfile)){
			username = strtok(buf, ":");
			if(username){
				unpwd = _mosquitto_calloc(1, sizeof(struct _mosquitto_unpwd));
				if(!unpwd) return MOSQ_ERR_NOMEM;
				unpwd->username = _mosquitto_strdup(username);
				if(!unpwd) return MOSQ_ERR_NOMEM;
				len = strlen(unpwd->username);
				while(unpwd->username[len-1] == 10 || unpwd->username[len-1] == 13){
					unpwd->username[len-1] = '\0';
					len = strlen(unpwd->username);
				}
				password = strtok(NULL, ":");
				if(password){
					unpwd->password = _mosquitto_strdup(password);
					if(!unpwd) return MOSQ_ERR_NOMEM;
					len = strlen(unpwd->password);
					while(unpwd->password[len-1] == 10 || unpwd->password[len-1] == 13){
						unpwd->password[len-1] = '\0';
						len = strlen(unpwd->password);
					}
				}
				unpwd->next = db->unpwd;
				db->unpwd = unpwd;
			}
		}
	}
	fclose(pwfile);

	return MOSQ_ERR_SUCCESS;
}

int mqtt3_unpwd_check(struct _mosquitto_db *db, const char *username, const char *password)
{
	struct _mosquitto_unpwd *tail;

	if(!db || !username) return MOSQ_ERR_INVAL;

	tail = db->unpwd;
	while(tail){
		if(!strcmp(tail->username, username)){
			if(tail->password){
				if(password){
					if(!strcmp(tail->password, password)){
						return MOSQ_ERR_SUCCESS;
					}
				}else{
					return MOSQ_ERR_AUTH;
				}
			}else{
				return MOSQ_ERR_SUCCESS;
			}
		}
		tail = tail->next;
	}

	return MOSQ_ERR_AUTH;
}

int mqtt3_unpwd_cleanup(struct _mosquitto_db *db)
{
	struct _mosquitto_unpwd *tail;

	if(!db) return MOSQ_ERR_INVAL;

	while(db->unpwd){
		tail = db->unpwd->next;
		if(db->unpwd->password) _mosquitto_free(db->unpwd->password);
		if(db->unpwd->username) _mosquitto_free(db->unpwd->username);
		_mosquitto_free(db->unpwd);
		db->unpwd = tail;
	}

	return MOSQ_ERR_SUCCESS;
}

