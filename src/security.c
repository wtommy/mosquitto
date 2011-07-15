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

int mosquitto_security_init(mosquitto_db *db)
{
	int rc;

#ifdef WITH_EXTERNAL_SECURITY_CHECKS
	rc = mosquitto_unpwd_init(db);
	if(rc){
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error initialising passwords.");
		return rc;
	}

	rc = mosquitto_acl_init(db);
	if(rc){
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error initialising ACLs.");
		return rc;
	}
#else
	/* Load username/password data if required. */
	if(db->config->password_file){
		rc = mqtt3_pwfile_parse(db);
		if(rc){
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error opening password file.");
			return rc;
		}
	}

	/* Load acl data if required. */
	if(db->config->acl_file){
		rc = mqtt3_aclfile_parse(db);
		if(rc){
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error opening acl file.");
			return rc;
		}
	}
#endif
	return MOSQ_ERR_SUCCESS;
}

void mosquitto_security_cleanup(mosquitto_db *db)
{
	mosquitto_acl_cleanup(db);
	mosquitto_unpwd_cleanup(db);
}

#ifndef WITH_EXTERNAL_SECURITY_CHECKS

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
		acl_user->acl = NULL;
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
		while(acl_tail->child){
			acl_tail = acl_tail->child;
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

int mosquitto_acl_check(struct _mosquitto_db *db, mqtt3_context *context, const char *topic, int access)
{
	char *local_topic;
	char *token;
	struct _mosquitto_acl *acl_root, *acl_tail;

	if(!db || !context || !topic) return MOSQ_ERR_INVAL;
	if(!db->acl_list) return MOSQ_ERR_SUCCESS;
	if(!context->acl_list) return MOSQ_ERR_ACL_DENIED;

	acl_root = context->acl_list->acl;

	/* Loop through all ACLs for this client. */
	while(acl_root){
		local_topic = _mosquitto_strdup(topic);
		if(!local_topic) return MOSQ_ERR_NOMEM;

		acl_tail = acl_root;

		if(local_topic[0] == '/'){
			if(strcmp(acl_tail->topic, "/")){
				acl_root = acl_root->next;
				continue;
			}
			acl_tail = acl_tail->child;
		}

		token = strtok(local_topic, "/");
		/* Loop through the topic looking for matches to this ACL. */
		while(token){
			if(acl_tail){
				if(!strcmp(acl_tail->topic, "#") && acl_tail->child == NULL){
					/* We have a match */
					if(access & acl_tail->access){
						/* And access is allowed. */
						_mosquitto_free(local_topic);
						return MOSQ_ERR_SUCCESS;
					}else{
						break;
					}
				}else if(!strcmp(acl_tail->topic, token) || !strcmp(acl_tail->topic, "+")){
					token = strtok(NULL, "/");
					if(!token && acl_tail->child == NULL){
						/* We have a match */
						if(access & acl_tail->access){
							/* And access is allowed. */
							_mosquitto_free(local_topic);
							return MOSQ_ERR_SUCCESS;
						}else{
							break;
						}
					}
				}else{
					break;
				}
				acl_tail = acl_tail->child;
			}else{
				break;
			}
		}
		_mosquitto_free(local_topic);

		acl_root = acl_root->next;
	}

	return MOSQ_ERR_ACL_DENIED;
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
	int slen;

	if(!db || !db->config) return MOSQ_ERR_INVAL;
	if(!db->config->acl_file) return MOSQ_ERR_SUCCESS;

	aclfile = fopen(db->config->acl_file, "rt");
	if(!aclfile) return 1;

	// topic [read|write] <topic> 
	// user <user>

	while(fgets(buf, 1024, aclfile)){
		slen = strlen(buf);
		while(buf[slen-1] == 10 || buf[slen-1] == 13){
			buf[slen-1] = '\0';
			slen = strlen(buf);
		}
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
					access = MOSQ_ACL_READ | MOSQ_ACL_WRITE;
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

	if(user) _mosquitto_free(user);
	fclose(aclfile);

	return MOSQ_ERR_SUCCESS;
}

static void _free_acl(struct _mosquitto_acl *acl)
{
	if(!acl) return;

	if(acl->child){
		_free_acl(acl->child);
	}
	if(acl->next){
		_free_acl(acl->next);
	}
	if(acl->topic){
		_mosquitto_free(acl->topic);
	}
	_mosquitto_free(acl);
}

void mosquitto_acl_cleanup(struct _mosquitto_db *db)
{
	int i;
	struct _mosquitto_acl_user *user_tail;

	if(!db || !db->acl_list) return;

	/* As we're freeing ACLs, we must clear context->acl_list to ensure no
	 * invalid memory accesses take place later.
	 * This *requires* the ACLs to be reapplied after mosquitto_acl_cleanup()
	 * is called if we are reloading the config. If this is not done, all 
	 * access will be denied to currently connected clients.
	 */
	if(db->contexts){
		for(i=0; i<db->context_count; i++){
			if(db->contexts[i] && db->contexts[i]->acl_list){
				db->contexts[i]->acl_list = NULL;
			}
		}
	}

	while(db->acl_list){
		user_tail = db->acl_list->next;

		_free_acl(db->acl_list->acl);
		if(db->acl_list->username){
			_mosquitto_free(db->acl_list->username);
		}
		_mosquitto_free(db->acl_list);
		
		db->acl_list = user_tail;
	}
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

int mosquitto_unpwd_check(struct _mosquitto_db *db, const char *username, const char *password)
{
	struct _mosquitto_unpwd *tail;

	if(!db || !username) return MOSQ_ERR_INVAL;
	if(!db->unpwd) return MOSQ_ERR_SUCCESS;

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

int mosquitto_unpwd_cleanup(struct _mosquitto_db *db)
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

/* Apply security settings after a reload.
 * Includes:
 * - Disconnecting anonymous users if appropriate
 * - Disconnecting users with invalid passwords
 * - Reapplying ACLs
 */
int mosquitto_security_apply(struct _mosquitto_db *db)
{
	struct _mosquitto_acl_user *acl_user_tail;
	struct _mosquitto_unpwd *unpwd_tail;
	bool allow_anonymous;
	int i;
	bool unpwd_ok;

	if(!db) return MOSQ_ERR_INVAL;

	allow_anonymous = db->config->allow_anonymous;
	
	if(db->contexts){
		for(i=0; i<db->context_count; i++){
			if(db->contexts[i]){
				/* Check for anonymous clients when allow_anonymous is false */
				if(!allow_anonymous && !db->contexts[i]->core.username){
					db->contexts[i]->core.state = mosq_cs_disconnecting;
					_mosquitto_socket_close(&db->contexts[i]->core);
					continue;
				}
				/* Check for connected clients that are no longer authorised */
				if(db->unpwd && db->contexts[i]->core.username){
					unpwd_ok = false;
					unpwd_tail = db->unpwd;
					while(unpwd_tail){
						if(!strcmp(db->contexts[i]->core.username, unpwd_tail->username)){
							if(unpwd_tail->password){
								if(!db->contexts[i]->core.password 
										|| strcmp(db->contexts[i]->core.password, unpwd_tail->password)){

									/* Non matching password to username. */
									db->contexts[i]->core.state = mosq_cs_disconnecting;
									_mosquitto_socket_close(&db->contexts[i]->core);
									continue;
								}else{
									/* Username matches, password matches. */
									unpwd_ok = true;
								}
							}else{
								/* Username matches, password not in password file. */
								unpwd_ok = true;
							}
						}
						unpwd_tail = unpwd_tail->next;
					}
					if(!unpwd_ok){
						db->contexts[i]->core.state = mosq_cs_disconnecting;
						_mosquitto_socket_close(&db->contexts[i]->core);
						continue;
					}
				}
				/* Check for ACLs and apply to user. */
				if(db->acl_list){
  					acl_user_tail = db->acl_list;
					while(acl_user_tail){
						if(acl_user_tail->username){
							if(db->contexts[i]->core.username){
								if(!strcmp(acl_user_tail->username, db->contexts[i]->core.username)){
									db->contexts[i]->acl_list = acl_user_tail;
									break;
								}
							}
						}else{
							if(!db->contexts[i]->core.username){
								db->contexts[i]->acl_list = acl_user_tail;
								break;
							}
						}
						acl_user_tail = acl_user_tail->next;
					}
				}
			}
		}
	}
	return MOSQ_ERR_SUCCESS;
}

#endif
