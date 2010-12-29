#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <config.h>
#include <mqtt3.h>
#include <memory_mosq.h>

static int _mqtt3_conf_parse_bool(char **token, const char *name, bool *value);
static int _mqtt3_conf_parse_int(char **token, const char *name, int *value);

void mqtt3_config_init(mqtt3_config *config)
{
	/* Set defaults */
	config->allow_anonymous = false;
	config->autosave_interval = 1800;
	config->daemon = false;
	config->default_listener.host = NULL;
	config->default_listener.port = 0;
	config->listeners = NULL;
	config->listener_count = 0;
#ifndef WIN32
	config->log_dest = MQTT3_LOG_STDERR;
	config->log_type = MOSQ_LOG_ERR | MOSQ_LOG_WARNING | MOSQ_LOG_NOTICE | MOSQ_LOG_INFO;
#else
	config->log_dest = MQTT3_LOG_SYSLOG;
	config->log_type = MOSQ_LOG_ERR | MOSQ_LOG_WARNING;
#endif
	config->max_connections = -1;
	config->password_file = NULL;
	config->persistence = false;
	config->persistence_location = NULL;
	config->persistence_file = "mosquitto.db";
	config->pid_file = NULL;
	config->retry_interval = 20;
	config->store_clean_interval = 10;
	config->sys_interval = 10;
	config->user = "mosquitto";
#ifdef WITH_BRIDGE
	config->bridges = NULL;
	config->bridge_count = 0;
#endif
}

int mqtt3_config_parse_args(mqtt3_config *config, int argc, char *argv[])
{
	int i;
	int port_tmp;

	for(i=1; i<argc; i++){
		if(!strcmp(argv[i], "-c") || !strcmp(argv[i], "--config-file")){
			if(i<argc-1){
				if(mqtt3_config_read(config, argv[i+1])){
					mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Unable to open configuration file.");
					return 1;
				}
			}else{
				mqtt3_log_printf(MOSQ_LOG_ERR, "Error: -c argument given, but no config file specified.");
				return MOSQ_ERR_INVAL;
			}
			i++;
		}else if(!strcmp(argv[i], "-d") || !strcmp(argv[i], "--daemon")){
			config->daemon = true;
		}else if(!strcmp(argv[i], "-p") || !strcmp(argv[i], "--port")){
			if(i<argc-1){
				port_tmp = atoi(argv[i+1]);
				if(port_tmp<1 || port_tmp>65535){
					mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid port specified (%d).", port_tmp);
					return MOSQ_ERR_INVAL;
				}else{
					if(config->default_listener.port){
						mqtt3_log_printf(MOSQ_LOG_WARNING, "Warning: Default listener port specified multiple times. Only the latest will be used.");
					}
					config->default_listener.port = port_tmp;
				}
			}else{
				mqtt3_log_printf(MOSQ_LOG_ERR, "Error: -p argument given, but no port specified.");
				return MOSQ_ERR_INVAL;
			}
		}
	}

	if(config->listener_count == 0 || config->default_listener.host || config->default_listener.port){
		config->listener_count++;
		config->listeners = _mosquitto_realloc(config->listeners, sizeof(struct _mqtt3_listener)*config->listener_count);
		if(!config->listeners){
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory.");
			return MOSQ_ERR_NOMEM;
		}
		if(config->default_listener.port){
			config->listeners[config->listener_count-1].port = config->default_listener.port;
		}else{
			config->listeners[config->listener_count-1].port = 1883;
		}
		if(config->default_listener.host){
			config->listeners[config->listener_count-1].host = config->default_listener.host;
		}else{
			config->listeners[config->listener_count-1].host = NULL;
		}
		
	}

	return MOSQ_ERR_SUCCESS;
}

int mqtt3_config_read(mqtt3_config *config, const char *filename)
{
	int rc = MOSQ_ERR_SUCCESS;
	FILE *fptr = NULL;
	char buf[1024];
	char *token;
	int port_tmp;
	int log_dest = MQTT3_LOG_NONE;
	int log_dest_set = 0;
	int log_type = MOSQ_LOG_NONE;
	int log_type_set = 0;
	int i;
	struct _mqtt3_bridge *cur_bridge = NULL;
	int max_inflight_messages = 20;
	int max_queued_messages = 100;
	
	fptr = fopen(filename, "rt");
	if(!fptr) return 1;

	while(fgets(buf, 1024, fptr)){
		if(buf[0] != '#' && buf[0] != 10 && buf[0] != 13){
			while(buf[strlen(buf)-1] == 10 || buf[strlen(buf)-1] == 13){
				buf[strlen(buf)-1] = 0;
			}
			token = strtok(buf, " ");
			if(token){
				if(!strcmp(token, "address") || !strcmp(token, "addresses")){
#ifdef WITH_BRIDGE
					if(!cur_bridge || cur_bridge->address){
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					token = strtok(NULL, " ");
					if(token){
						token = strtok(token, ":");
						if(token){
							cur_bridge->address = _mosquitto_strdup(token);
							if(!cur_bridge->address){
								mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory.");
								return MOSQ_ERR_NOMEM;
							}
							token = strtok(NULL, ":");
							if(token){
								port_tmp = atoi(token);
								if(port_tmp < 1 || port_tmp > 65535){
									mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid port value (%d).", port_tmp);
									return MOSQ_ERR_INVAL;
								}
								cur_bridge->port = port_tmp;
							}else{
								cur_bridge->port = 1883;
							}
						}
					}else{
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty address value in configuration.");
						return MOSQ_ERR_INVAL;
					}
#else
					mqtt3_log_printf(MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "allow_anonymous")){
					if(_mqtt3_conf_parse_bool(&token, "allow_anonymous", &config->allow_anonymous)) return 1;
				}else if(!strcmp(token, "autosave_interval")){
					if(_mqtt3_conf_parse_int(&token, "autosave_interval", &config->autosave_interval)) return 1;
					if(config->autosave_interval < 0) config->autosave_interval = 0;
				}else if(!strcmp(token, "bind_address")){
					token = strtok(NULL, " ");
					if(token){
						if(config->default_listener.host){
							mqtt3_log_printf(MOSQ_LOG_WARNING, "Warning: Default listener bind_address specified multiple times. Only the latest will be used.");
							_mosquitto_free(config->default_listener.host);
						}
						config->default_listener.host = _mosquitto_strdup(token);
					}else{
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty bind_address value in configuration.");
						return MOSQ_ERR_INVAL;
					}
				}else if(!strcmp(token, "ext_sqlite_regex")){
					mqtt3_log_printf(MOSQ_LOG_WARNING, "Warning: ext_sqlite_regex variable no longer in use.");
				}else if(!strcmp(token, "cleansession")){
#ifdef WITH_BRIDGE
					if(!cur_bridge){
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					if(_mqtt3_conf_parse_bool(&token, "cleansession", &cur_bridge->clean_session)) return 1;
#else
					mqtt3_log_printf(MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "connection")){
#ifdef WITH_BRIDGE
					token = strtok(NULL, " ");
					if(token){
						config->bridge_count++;
						config->bridges = _mosquitto_realloc(config->bridges, config->bridge_count*sizeof(struct _mqtt3_bridge));
						if(!config->bridges){
							mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory.");
							return MOSQ_ERR_NOMEM;
						}
						cur_bridge = &(config->bridges[config->bridge_count-1]);
						cur_bridge->name = _mosquitto_strdup(token);
						cur_bridge->address = NULL;
						cur_bridge->keepalive = 60;
						cur_bridge->clean_session = false;
						cur_bridge->port = 0;
						cur_bridge->topics = NULL;
						cur_bridge->topic_count = 0;
						cur_bridge->restart_t = 0;
						cur_bridge->username = NULL;
						cur_bridge->password = NULL;
					}else{
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty connection value in configuration.");
						return MOSQ_ERR_INVAL;
					}
#else
					mqtt3_log_printf(MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "keepalive_interval")){
#ifdef WITH_BRIDGE
					if(!cur_bridge){
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					if(_mqtt3_conf_parse_int(&token, "keepalive_interval", &cur_bridge->keepalive)) return 1;
					if(cur_bridge->keepalive < 5){
						mqtt3_log_printf(MOSQ_LOG_NOTICE, "keepalive interval too low, using 5 seconds.");
						cur_bridge->keepalive = 5;
					}
#else
					mqtt3_log_printf(MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "listener")){
					token = strtok(NULL, " ");
					if(token){
						config->listener_count++;
						config->listeners = _mosquitto_realloc(config->listeners, sizeof(struct _mqtt3_listener)*config->listener_count);
						if(!config->listeners){
							mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory.");
							return MOSQ_ERR_NOMEM;
						}
						port_tmp = atoi(token);
						if(port_tmp < 1 || port_tmp > 65535){
							mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid port value (%d).", port_tmp);
							return MOSQ_ERR_INVAL;
						}
						config->listeners[config->listener_count-1].port = port_tmp;
						token = strtok(NULL, " ");
						if(token){
							config->listeners[config->listener_count-1].host = _mosquitto_strdup(token);
						}else{
							config->listeners[config->listener_count-1].host = NULL;
						}
					}else{
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty listener value in configuration.");
						return MOSQ_ERR_INVAL;
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
							mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid log_dest value (%s).", token);
							return MOSQ_ERR_INVAL;
						}
					}else{
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty log_dest value in configuration.");
						return MOSQ_ERR_INVAL;
					}
				}else if(!strcmp(token, "log_type")){
					token = strtok(NULL, " ");
					if(token){
						log_type_set = 1;
						if(!strcmp(token, "none")){
							log_type = MOSQ_LOG_NONE;
						}else if(!strcmp(token, "information")){
							log_type |= MOSQ_LOG_INFO;
						}else if(!strcmp(token, "notice")){
							log_type |= MOSQ_LOG_NOTICE;
						}else if(!strcmp(token, "warning")){
							log_type |= MOSQ_LOG_WARNING;
						}else if(!strcmp(token, "error")){
							log_type |= MOSQ_LOG_ERR;
						}else if(!strcmp(token, "debug")){
							log_type |= MOSQ_LOG_DEBUG;
						}else{
							mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid log_type value (%s).", token);
							return MOSQ_ERR_INVAL;
						}
					}else{
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty log_type value in configuration.");
					}
				}else if(!strcmp(token, "max_connections")){
					token = strtok(NULL, " ");
					if(token){
						config->max_connections = atoi(token);
						if(config->max_connections < 0) config->max_connections = -1;
					}else{
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty max_connections value in configuration.");
					}
				}else if(!strcmp(token, "max_inflight_messages")){
					token = strtok(NULL, " ");
					if(token){
						max_inflight_messages = atoi(token);
						if(max_inflight_messages < 0) max_inflight_messages = 0;
					}else{
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty max_inflight_messages value in configuration.");
					}
				}else if(!strcmp(token, "max_queued_messages")){
					token = strtok(NULL, " ");
					if(token){
						max_queued_messages = atoi(token);
						if(max_queued_messages < 0) max_queued_messages = 0;
					}else{
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty max_queued_messages value in configuration.");
					}
				}else if(!strcmp(token, "password")){
#ifdef WITH_BRIDGE
					if(!cur_bridge){
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					token = strtok(NULL, " ");
					if(token){
						if(cur_bridge->password){
							mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Duplicate password value in bridge configuration.");
							return MOSQ_ERR_INVAL;
						}
						cur_bridge->password = _mosquitto_strdup(token);
						if(!cur_bridge->password){
							mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory");
							return MOSQ_ERR_NOMEM;
						}
					}else{
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty password value in configuration.");
						return MOSQ_ERR_INVAL;
					}
#else
					mqtt3_log_printf(MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "password_file")){
					token = strtok(NULL, " ");
					if(token){
						config->password_file = _mosquitto_strdup(token);
					}else{
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty password_file value in configuration.");
						return MOSQ_ERR_INVAL;
					}
				}else if(!strcmp(token, "persistence")){
					if(_mqtt3_conf_parse_bool(&token, "persistence", &config->persistence)) return 1;
				}else if(!strcmp(token, "persistence_file")){
					token = strtok(NULL, " ");
					if(token){
						config->persistence_file = _mosquitto_strdup(token);
					}else{
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty persistence_file value in configuration.");
						return MOSQ_ERR_INVAL;
					}
				}else if(!strcmp(token, "persistence_location")){
					token = strtok(NULL, " ");
					if(token){
						config->persistence_location = _mosquitto_strdup(token);
						if(token[strlen(token)-1] != '/'){
							mqtt3_log_printf(MOSQ_LOG_WARNING, "Warning: persistence_location should normally end with a '/'.");
						}
					}
				}else if(!strcmp(token, "pid_file")){
					token = strtok(NULL, " ");
					if(token){
						config->pid_file = _mosquitto_strdup(token);
					}
				}else if(!strcmp(token, "port")){
					if(config->default_listener.port){
						mqtt3_log_printf(MOSQ_LOG_WARNING, "Warning: Default listener port specified multiple times. Only the latest will be used.");
					}
					if(_mqtt3_conf_parse_int(&token, "port", &port_tmp)) return 1;
					if(port_tmp < 1 || port_tmp > 65535){
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid port value (%d).", port_tmp);
						return MOSQ_ERR_INVAL;
					}
					config->default_listener.port = port_tmp;
				}else if(!strcmp(token, "retry_interval")){
					if(_mqtt3_conf_parse_int(&token, "retry_interval", &config->retry_interval)) return 1;
					if(config->retry_interval < 1 || config->retry_interval > 3600){
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid retry_interval value (%d).", config->retry_interval);
						return MOSQ_ERR_INVAL;
					}
				}else if(!strcmp(token, "store_clean_interval")){
					if(_mqtt3_conf_parse_int(&token, "store_clean_interval", &config->store_clean_interval)) return 1;
					if(config->store_clean_interval < 0 || config->store_clean_interval > 65535){
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid store_clean_interval value (%d).", config->store_clean_interval);
						return MOSQ_ERR_INVAL;
					}
				}else if(!strcmp(token, "sys_interval")){
					if(_mqtt3_conf_parse_int(&token, "sys_interval", &config->sys_interval)) return 1;
					if(config->sys_interval < 1 || config->sys_interval > 65535){
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid sys_interval value (%d).", config->sys_interval);
						return MOSQ_ERR_INVAL;
					}
				}else if(!strcmp(token, "topic")){
#ifdef WITH_BRIDGE
					if(!cur_bridge){
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					token = strtok(NULL, " ");
					if(token){
						cur_bridge->topic_count++;
						cur_bridge->topics = _mosquitto_realloc(cur_bridge->topics, 
								sizeof(struct _mqtt3_bridge_topic)*cur_bridge->topic_count);
						if(!cur_bridge->topics){
							mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory");
							return MOSQ_ERR_NOMEM;
						}
						cur_bridge->topics[cur_bridge->topic_count-1].topic = _mosquitto_strdup(token);
						if(!cur_bridge->topics[cur_bridge->topic_count-1].topic){
							mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory");
							return MOSQ_ERR_NOMEM;
						}
						cur_bridge->topics[cur_bridge->topic_count-1].direction = bd_out;
					}else{
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty topic value in configuration.");
						return MOSQ_ERR_INVAL;
					}
					token = strtok(NULL, " ");
					if(token){
						if(!strcasecmp(token, "out")){
							cur_bridge->topics[cur_bridge->topic_count-1].direction = bd_out;
						}else if(!strcasecmp(token, "in")){
							cur_bridge->topics[cur_bridge->topic_count-1].direction = bd_in;
						}else if(!strcasecmp(token, "both")){
							cur_bridge->topics[cur_bridge->topic_count-1].direction = bd_both;
						}else{
							mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid bridge topic direction '%s'.", token);
							return MOSQ_ERR_INVAL;
						}
					}
#else
					mqtt3_log_printf(MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "user")){
					token = strtok(NULL, " ");
					if(token){
						config->user = _mosquitto_strdup(token);
					}else{
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid user value.");
						return MOSQ_ERR_INVAL;
					}
				}else if(!strcmp(token, "username")){
#ifdef WITH_BRIDGE
					if(!cur_bridge){
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					token = strtok(NULL, " ");
					if(token){
						if(cur_bridge->username){
							mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Duplicate username value in bridge configuration.");
							return MOSQ_ERR_INVAL;
						}
						cur_bridge->username = _mosquitto_strdup(token);
						if(!cur_bridge->username){
							mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Out of memory");
							return MOSQ_ERR_NOMEM;
						}
					}else{
						mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty username value in configuration.");
						return MOSQ_ERR_INVAL;
					}
#else
					mqtt3_log_printf(MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "autosave_on_changes")
						|| !strcmp(token, "clientid_prefixes")
						|| !strcmp(token, "connection_messages")
						|| !strcmp(token, "retained_persistence")
						|| !strcmp(token, "trace_level")
						|| !strcmp(token, "addresses")
						|| !strcmp(token, "idle_timeout")
						|| !strcmp(token, "notifications")
						|| !strcmp(token, "notification_topic")
						|| !strcmp(token, "round_robin")
						|| !strcmp(token, "start_type")
						|| !strcmp(token, "threshold")
						|| !strcmp(token, "try_private")
						|| !strcmp(token, "mount_point")
						|| !strcmp(token, "clientid")
						|| !strcmp(token, "acl_file")
						|| !strcmp(token, "ffdc_output")
						|| !strcmp(token, "max_log_entries")
						|| !strcmp(token, "password_file")
						|| !strcmp(token, "trace_output")){
					mqtt3_log_printf(MOSQ_LOG_WARNING, "Warning: Unsupported rsmb configuration option \"%s\".", token);
				}else{
					mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Unknown configuration variable \"%s\".", token);
					return MOSQ_ERR_INVAL;
				}
			}
		}
	}
	fclose(fptr);

	mqtt3_db_limits_set(max_inflight_messages, max_queued_messages);
	mqtt3_net_set_max_connections(config->max_connections);

	for(i=0; i<config->bridge_count; i++){
		if(!config->bridges[i].name || !config->bridges[i].address || !config->bridges[i].port || !config->bridges[i].topic_count){
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
			return MOSQ_ERR_INVAL;
		}
	}

	if(log_dest_set){
		config->log_dest = log_dest;
	}
	if(log_type_set){
		config->log_type = log_type;
	}

	return rc;
}

static int _mqtt3_conf_parse_bool(char **token, const char *name, bool *value)
{
	*token = strtok(NULL, " ");
	if(*token){
		if(!strcmp(*token, "false") || !strcmp(*token, "0")){
			*value = false;
		}else if(!strcmp(*token, "true") || !strcmp(*token, "1")){
			*value = true;
		}else{
			mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Invalid %s value (%s).", name, *token);
		}
	}else{
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty %s value in configuration.", name);
		return MOSQ_ERR_INVAL;
	}
	
	return MOSQ_ERR_SUCCESS;
}

static int _mqtt3_conf_parse_int(char **token, const char *name, int *value)
{
	*token = strtok(NULL, " ");
	if(*token){
		*value = atoi(*token);
	}else{
		mqtt3_log_printf(MOSQ_LOG_ERR, "Error: Empty %s value in configuration.", name);
		return MOSQ_ERR_INVAL;
	}

	return MOSQ_ERR_SUCCESS;
}
