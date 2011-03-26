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

int mqtt3_pwfile_parse(struct _mosquitto_db *db)
{
	FILE *pwfile;
	struct _mosquitto_unpwd *unpwd;
	char buf[256];
	char *username, *password;

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
				password = strtok(NULL, ":");
				if(password){
					unpwd->password = _mosquitto_strdup(password);
					if(!unpwd) return MOSQ_ERR_NOMEM;
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

