#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mqtt3.h>

void mqtt3_config_init(mqtt3_config *config)
{
	/* Set defaults */
	config->daemon = 0;
	config->iface = NULL;
	config->iface_count = 0;
	config->log_dest = MQTT3_LOG_STDERR | MQTT3_LOG_TOPIC;
	config->log_priorities = MQTT3_LOG_ERR | MQTT3_LOG_WARNING;
	config->msg_timeout = 10;
	config->persistence = 0;
	config->persistence_location = NULL;
	config->pid_file = NULL;
	config->sys_interval = 10;
	config->user = "mosquitto";
}

int mqtt3_config_parse_args(mqtt3_config *config, int argc, char *argv[])
{
	int i;
	int port_tmp;
	char *str;

	for(i=1; i<argc; i++){
		if(!strcmp(argv[i], "-c") || !strcmp(argv[i], "--config-file")){
			if(i<argc-1){
				if(mqtt3_config_read(config, argv[i+1])){
					fprintf(stderr, "Error: Unable to open configuration file.\n");
					return 1;
				}
			}else{
				fprintf(stderr, "Error: -c argument given, but no config file specified.\n");
				return 1;
			}
			i++;
		}else if(!strcmp(argv[i], "-d") || !strcmp(argv[i], "--daemon")){
			config->daemon = 1;
		}else if(!strcmp(argv[i], "-i") || !strcmp(argv[i], "--interface")){
			if(i<argc-1){
				i++;
				str = strtok(argv[i], ":");
				if(str){
					config->iface_count++;
					config->iface = mqtt3_realloc(config->iface, sizeof(struct mqtt3_iface)*config->iface_count);
					if(!config->iface){
						fprintf(stderr, "Error: Out of memory.\n");
						return 1;
					}
					config->iface[config->iface_count-1].iface = mqtt3_strdup(str);
					str = strtok(NULL, ":");
					if(str){
						port_tmp = atoi(str);
						if(port_tmp < 1 || port_tmp > 65535){
							fprintf(stderr, "Error: Invalid port value (%d).\n", port_tmp);
							return 1;
						}
						config->iface[config->iface_count-1].port = port_tmp;
					}else{
						config->iface[config->iface_count-1].port = 1883;
					}
				}
			}else{
				fprintf(stderr, "Error: -i argument given, but no interface specifed.\n");
				return 1;
			}
		}else if(!strcmp(argv[i], "-p") || !strcmp(argv[i], "--port")){
			if(i<argc-1){
				port_tmp = atoi(argv[i+1]);
				if(port_tmp<1 || port_tmp>65535){
					fprintf(stderr, "Error: Invalid port specified (%d).\n", port_tmp);
					return 1;
				}else{
					config->iface_count++;
					config->iface = mqtt3_realloc(config->iface, sizeof(struct mqtt3_iface)*config->iface_count);
					if(!config->iface){
						fprintf(stderr, "Error: Out of memory.\n");
						return 1;
					}
					config->iface[config->iface_count-1].iface = NULL;
					config->iface[config->iface_count-1].port = port_tmp;
				}
			}else{
				fprintf(stderr, "Error: -p argument given, but no port specified.\n");
				return 1;
			}
		}
	}

	if(!config->iface){
		config->iface = mqtt3_malloc(sizeof(struct mqtt3_iface));
		config->iface->iface = NULL;
		config->iface->port = 1883;
		config->iface_count = 1;
	}
	return 0;
}

int mqtt3_config_read(mqtt3_config *config, const char *filename)
{
	int rc = 0;
	FILE *fptr = NULL;
	char buf[1024];
	char *token;
	int port_tmp;
	
	fptr = fopen(filename, "rt");
	if(!fptr) return 1;

	while(fgets(buf, 1024, fptr)){
		if(buf[0] != '#' && buf[0] != 10 && buf[0] != 13){
			while(buf[strlen(buf)-1] == 10 || buf[strlen(buf)-1] == 13){
				buf[strlen(buf)-1] = 0;
			}
			token = strtok(buf, " ");
			if(token){

				if(!strcmp(token, "interface")){
					token = strtok(NULL, " ");
					if(token){
						token = strtok(token, ":");
						if(token){
							config->iface_count++;
							config->iface = mqtt3_realloc(config->iface, sizeof(struct mqtt3_iface)*config->iface_count);
							if(!config->iface){
								fprintf(stderr, "Error: Out of memory.\n");
								return 1;
							}
							config->iface[config->iface_count-1].iface = mqtt3_strdup(token);
							token = strtok(NULL, ":");
							if(token){
								port_tmp = atoi(token);
								if(port_tmp < 1 || port_tmp > 65535){
									fprintf(stderr, "Error: Invalid port value (%d).\n", port_tmp);
									return 1;
								}
								config->iface[config->iface_count-1].port = port_tmp;
							}else{
								config->iface[config->iface_count-1].port = 1883;
							}
						}
					}else{
						fprintf(stderr, "Warning: Empty interface value in configuration.\n");
					}
				}else if(!strcmp(token, "log_dest")){
					token = strtok(NULL, " ");
					if(token){
						if(!strcmp(token, "none")){
							config->log_dest = MQTT3_LOG_NONE;
						}else if(!strcmp(token, "syslog")){
							config->log_dest |= MQTT3_LOG_SYSLOG;
						}else if(!strcmp(token, "stdout")){
							config->log_dest |= MQTT3_LOG_STDOUT;
						}else if(!strcmp(token, "stderr")){
							config->log_dest |= MQTT3_LOG_STDERR;
						}else if(!strcmp(token, "topic")){
							config->log_dest |= MQTT3_LOG_TOPIC;
						}else{
							fprintf(stderr, "Error: Invalid log_dest value (%s).\n", token);
							return 1;
						}
					}else{
						fprintf(stderr, "Warning: Empty log_dest value in configuration.\n");
					}
				}else if(!strcmp(token, "log_type")){
					token = strtok(NULL, " ");
					if(token){
						if(!strcmp(token, "none")){
							config->log_priorities = MQTT3_LOG_NONE;
						}else if(!strcmp(token, "information")){
							config->log_priorities |= MQTT3_LOG_INFO;
						}else if(!strcmp(token, "notice")){
							config->log_priorities |= MQTT3_LOG_NOTICE;
						}else if(!strcmp(token, "warning")){
							config->log_priorities |= MQTT3_LOG_WARNING;
						}else if(!strcmp(token, "error")){
							config->log_priorities |= MQTT3_LOG_ERR;
						}else if(!strcmp(token, "debug")){
							config->log_priorities |= MQTT3_LOG_DEBUG;
						}else{
							fprintf(stderr, "Error: Invalid log_type value (%s).\n", token);
							return 1;
						}
					}else{
						fprintf(stderr, "Warning: Empty log_type value in configuration.\n");
					}
				}else if(!strcmp(token, "msg_timeout")){
					token = strtok(NULL, " ");
					if(token){
						config->msg_timeout = atoi(token);
						if(config->msg_timeout < 1 || config->msg_timeout > 3600){
							fprintf(stderr, "Warning: Invalid msg_timeout value (%d). Using default (10).\n", config->msg_timeout);
						}
					}else{
						fprintf(stderr, "Warning: Empty msg_timeout value in configuration.\n");
					}
				}else if(!strcmp(token, "persistence")){
					token = strtok(NULL, " ");
					if(token){
						config->persistence = atoi(token);
						if(config->persistence != 1 && config->persistence != 0){
							fprintf(stderr, "Warning: Invalid persistence value (%d). Using default (0).\n", config->persistence);
							config->persistence = 0;
						}
					}else{
						fprintf(stderr, "Warning: Empty persistence value in configuration.\n");
					}
				}else if(!strcmp(token, "persistence_location")){
					token = strtok(NULL, " ");
					if(token){
						config->persistence_location = mqtt3_strdup(token);
						if(token[strlen(token)-1] != '/'){
							fprintf(stderr, "Warning: persistence_location should normally end with a '/'.\n");
						}
					}
				}else if(!strcmp(token, "pid_file")){
					token = strtok(NULL, " ");
					if(token){
						config->pid_file = mqtt3_strdup(token);
					}
				}else if(!strcmp(token, "port")){
					token = strtok(NULL, " ");
					if(token){
						port_tmp = atoi(token);
						if(port_tmp < 1 || port_tmp > 65535){
							fprintf(stderr, "Error: Invalid port value (%d).\n", port_tmp);
							return 1;
						}
						config->iface_count++;
						config->iface = mqtt3_realloc(config->iface, sizeof(struct mqtt3_iface)*config->iface_count);
						if(!config->iface){
							fprintf(stderr, "Error: Out of memory.\n");
							return 1;
						}
						config->iface[config->iface_count-1].iface = NULL;
						config->iface[config->iface_count-1].port = port_tmp;
					}else{
						fprintf(stderr, "Warning: Empty port value in configuration.\n");
					}
				}else if(!strcmp(token, "sys_interval")){
					token = strtok(NULL, " ");
					if(token){
						config->sys_interval = atoi(token);
						if(config->sys_interval < 1 || config->sys_interval > 65535){
							fprintf(stderr, "Warning: Invalid sys_interval value (%d). Using default (10).\n", config->sys_interval);
							config->sys_interval = 10;
						}
					}else{
						fprintf(stderr, "Warning: Empty sys_interval value in configuration.\n");
					}
				}else if(!strcmp(token, "user")){
					token = strtok(NULL, " ");
					if(token){
						config->user = mqtt3_strdup(token);
					}else{
						fprintf(stderr, "Warning: Invalid user value. Using default.\n");
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
