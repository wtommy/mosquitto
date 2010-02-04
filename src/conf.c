#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <config.h>
#include <mqtt3.h>

void mqtt3_config_init(mqtt3_config *config)
{
	/* Set defaults */
	config->autosave_interval = 1800;
	config->daemon = 0;
#ifdef __CYGWIN__
	config->ext_sqlite_regex = "./sqlite3-pcre.dll";
#else
	config->ext_sqlite_regex = "./sqlite3-pcre.so";
#endif
	config->iface = NULL;
	config->iface_count = 0;
	config->log_dest = MQTT3_LOG_STDERR;
	config->log_type = MQTT3_LOG_ERR | MQTT3_LOG_WARNING | MQTT3_LOG_NOTICE | MQTT3_LOG_INFO;
	config->persistence = 0;
	config->persistence_location = NULL;
	config->pid_file = NULL;
	config->retry_interval = 20;
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
					mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Unable to open configuration file.");
					return 1;
				}
			}else{
				mqtt3_log_printf(MQTT3_LOG_ERR, "Error: -c argument given, but no config file specified.");
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
						mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Out of memory.");
						return 1;
					}
					config->iface[config->iface_count-1].iface = mqtt3_strdup(str);
					str = strtok(NULL, ":");
					if(str){
						port_tmp = atoi(str);
						if(port_tmp < 1 || port_tmp > 65535){
							mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Invalid port value (%d).", port_tmp);
							return 1;
						}
						config->iface[config->iface_count-1].port = port_tmp;
					}else{
						config->iface[config->iface_count-1].port = 1883;
					}
				}
			}else{
				mqtt3_log_printf(MQTT3_LOG_ERR, "Error: -i argument given, but no interface specifed.");
				return 1;
			}
		}else if(!strcmp(argv[i], "-p") || !strcmp(argv[i], "--port")){
			if(i<argc-1){
				port_tmp = atoi(argv[i+1]);
				if(port_tmp<1 || port_tmp>65535){
					mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Invalid port specified (%d).", port_tmp);
					return 1;
				}else{
					config->iface_count++;
					config->iface = mqtt3_realloc(config->iface, sizeof(struct mqtt3_iface)*config->iface_count);
					if(!config->iface){
						mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Out of memory.");
						return 1;
					}
					config->iface[config->iface_count-1].iface = NULL;
					config->iface[config->iface_count-1].port = port_tmp;
				}
			}else{
				mqtt3_log_printf(MQTT3_LOG_ERR, "Error: -p argument given, but no port specified.");
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
	int log_dest = MQTT3_LOG_NONE;
	int log_dest_set = 0;
	int log_type = MQTT3_LOG_NONE;
	int log_type_set = 0;
	
	fptr = fopen(filename, "rt");
	if(!fptr) return 1;

	while(fgets(buf, 1024, fptr)){
		if(buf[0] != '#' && buf[0] != 10 && buf[0] != 13){
			while(buf[strlen(buf)-1] == 10 || buf[strlen(buf)-1] == 13){
				buf[strlen(buf)-1] = 0;
			}
			token = strtok(buf, " ");
			if(token){
				if(!strcmp(token, "autosave_interval")){
					token = strtok(NULL, " ");
					if(token){
						config->autosave_interval = atoi(token);
						if(config->autosave_interval < 0) config->autosave_interval = 0;
					}else{
						mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Empty autosave_interval value in configuration.");
						return 1;
					}
				}else if(!strcmp(token, "ext_sqlite_regex")){
					token = strtok(NULL, " ");
					if(token){
						config->ext_sqlite_regex = mqtt3_strdup(token);
					}else{
						mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Empty ext_sqlite_regex value in config.");
						return 1;
					}
				}else if(!strcmp(token, "interface")){
					token = strtok(NULL, " ");
					if(token){
						token = strtok(token, ":");
						if(token){
							config->iface_count++;
							config->iface = mqtt3_realloc(config->iface, sizeof(struct mqtt3_iface)*config->iface_count);
							if(!config->iface){
								mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Out of memory.");
								return 1;
							}
							config->iface[config->iface_count-1].iface = mqtt3_strdup(token);
							token = strtok(NULL, ":");
							if(token){
								port_tmp = atoi(token);
								if(port_tmp < 1 || port_tmp > 65535){
									mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Invalid port value (%d).", port_tmp);
									return 1;
								}
								config->iface[config->iface_count-1].port = port_tmp;
							}else{
								config->iface[config->iface_count-1].port = 1883;
							}
						}
					}else{
						mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Empty interface value in configuration.");
						return 1;
					}
				}else if(!strcmp(token, "log_dest")){
					token = strtok(NULL, " ");
					if(token){
						log_dest_set = 1;
						if(!strcmp(token, "none")){
							log_dest = MQTT3_LOG_NONE;
						}else if(!strcmp(token, "syslog")){
							log_dest |= MQTT3_LOG_SYSLOG;
						}else if(!strcmp(token, "stdout")){
							log_dest |= MQTT3_LOG_STDOUT;
						}else if(!strcmp(token, "stderr")){
							log_dest |= MQTT3_LOG_STDERR;
						}else if(!strcmp(token, "topic")){
							log_dest |= MQTT3_LOG_TOPIC;
						}else{
							mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Invalid log_dest value (%s).", token);
							return 1;
						}
					}else{
						mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Empty log_dest value in configuration.");
						return 1;
					}
				}else if(!strcmp(token, "log_type")){
					token = strtok(NULL, " ");
					if(token){
						log_type_set = 1;
						if(!strcmp(token, "none")){
							log_type = MQTT3_LOG_NONE;
						}else if(!strcmp(token, "information")){
							log_type |= MQTT3_LOG_INFO;
						}else if(!strcmp(token, "notice")){
							log_type |= MQTT3_LOG_NOTICE;
						}else if(!strcmp(token, "warning")){
							log_type |= MQTT3_LOG_WARNING;
						}else if(!strcmp(token, "error")){
							log_type |= MQTT3_LOG_ERR;
						}else if(!strcmp(token, "debug")){
							log_type |= MQTT3_LOG_DEBUG;
						}else{
							mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Invalid log_type value (%s).", token);
							return 1;
						}
					}else{
						mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Empty log_type value in configuration.");
					}
				}else if(!strcmp(token, "persistence")){
					token = strtok(NULL, " ");
					if(token){
						config->persistence = atoi(token);
						if(config->persistence != 1 && config->persistence != 0){
							mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Invalid persistence value (%d).", config->persistence);
							return 1;
						}
					}else{
						mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Empty persistence value in configuration.");
						return 1;
					}
				}else if(!strcmp(token, "persistence_location")){
					token = strtok(NULL, " ");
					if(token){
						config->persistence_location = mqtt3_strdup(token);
						if(token[strlen(token)-1] != '/'){
							mqtt3_log_printf(MQTT3_LOG_WARNING, "Warning: persistence_location should normally end with a '/'.");
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
							mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Invalid port value (%d).", port_tmp);
							return 1;
						}
						config->iface_count++;
						config->iface = mqtt3_realloc(config->iface, sizeof(struct mqtt3_iface)*config->iface_count);
						if(!config->iface){
							mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Out of memory.");
							return 1;
						}
						config->iface[config->iface_count-1].iface = NULL;
						config->iface[config->iface_count-1].port = port_tmp;
					}else{
						mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Empty port value in configuration.");
						return 1;
					}
				}else if(!strcmp(token, "retry_interval")){
					token = strtok(NULL, " ");
					if(token){
						config->retry_interval = atoi(token);
						if(config->retry_interval < 1 || config->retry_interval > 3600){
							mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Invalid retry_interval value (%d).", config->retry_interval);
							return 1;
						}
					}else{
						mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Empty retry_interval value in configuration.");
						return 1;
					}
				}else if(!strcmp(token, "sys_interval")){
					token = strtok(NULL, " ");
					if(token){
						config->sys_interval = atoi(token);
						if(config->sys_interval < 1 || config->sys_interval > 65535){
							mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Invalid sys_interval value (%d).", config->sys_interval);
							return 1;
						}
					}else{
						mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Empty sys_interval value in configuration.");
						return 1;
					}
				}else if(!strcmp(token, "user")){
					token = strtok(NULL, " ");
					if(token){
						config->user = mqtt3_strdup(token);
					}else{
						mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Invalid user value.");
						return 1;
					}
				}else{
					mqtt3_log_printf(MQTT3_LOG_ERR, "Error: Unknown configuration variable \"%s\".", token);
					return 1;
				}
			}
		}
	}
	fclose(fptr);

	if(log_dest_set){
		config->log_dest = log_dest;
	}
	if(log_type_set){
		config->log_type = log_type;
	}

	return rc;
}
