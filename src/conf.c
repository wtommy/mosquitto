#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mqtt3.h>

int mqtt3_config_read(mqtt3_config *config)
{
	int rc = 0;
	FILE *fptr = NULL;
	char buf[1024];
	char *token;
	
	/* Set defaults */
	config->port = 1883;
	config->msg_timeout = 10;
	config->persistence = 1;
	config->persistence_location = NULL;
	config->sys_interval = 10;
	config->pid_file = NULL;

	fptr = fopen(CONFIG_PATH "/mosquitto.conf", "rt");
	if(!fptr) fptr = fopen("mosquitto.conf", "rt");
	if(!fptr) return 1;

	while(fgets(buf, 1024, fptr)){
		if(buf[0] != '#' && buf[0] != 10 && buf[0] != 13){
			token = strtok(buf, " ");
			if(token){
				if(!strcmp(token, "msg_timeout")){
					token = strtok(NULL, " ");
					config->msg_timeout = atoi(token);
					if(config->msg_timeout < 1 || config->msg_timeout > 3600){
						fprintf(stderr, "Warning: Invalid msg_timeout value (%d). Using default (10).\n", config->msg_timeout);
					}
					token = strtok(NULL, " ");
				}else if(!strcmp(token, "persistence")){
					token = strtok(NULL, " ");
					config->persistence = atoi(token);
					if(config->persistence != 1 && config->persistence != 0){
						fprintf(stderr, "Warning: Invalid persistence value (%d). Using default (1).\n", config->persistence);
						config->persistence = 1;
					}
				}else if(!strcmp(token, "persistence_location")){
					token = strtok(NULL, " ");
					if(token){
						while(token[strlen(token)-1] == 10 || token[strlen(token)-1] == 13){
							token[strlen(token)-1] = 0;
						}
						config->persistence_location = mqtt3_strdup(token);
					}else{
						fprintf(stderr, "Warning: Invalid persistence_location value. Using default.\n");
					}
				}else if(!strcmp(token, "pid_file")){
					token = strtok(NULL, " ");
					if(token){
						while(token[strlen(token)-1] == 10 || token[strlen(token)-1] == 13){
							token[strlen(token)-1] = 0;
						}
						config->pid_file = mqtt3_strdup(token);
					}else{
						fprintf(stderr, "Warning: Invalid pid_file value. Using default.\n");
					}
				}else if(!strcmp(token, "port")){
					token = strtok(NULL, " ");
					config->port = atoi(token);
					if(config->port < 1 || config->port > 65535){
						fprintf(stderr, "Warning: Invalid port value (%d). Using default (1883).\n", config->port);
						config->port = 1883;
					}
				}else if(!strcmp(token, "sys_interval")){
					token = strtok(NULL, " ");
					config->sys_interval = atoi(token);
					if(config->sys_interval < 1 || config->sys_interval > 65535){
						fprintf(stderr, "Warning: Invalid sys_interval value (%d). Using default (10).\n", config->sys_interval);
						config->sys_interval = 10;
					}
				}else{
					fprintf(stderr, "Warning: Unknown configuration variable \"%s\".\n", token);
				}
			}
		}
	}
	fclose(fptr);

	return rc;
}
