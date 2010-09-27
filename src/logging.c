/*
Copyright (c) 2009,2010, Roger Light <roger@atchoo.org>
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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <config.h>
#include <mqtt3.h>

/* Options for logging should be:
 *
 * A combination of:
 * Via syslog
 * To a file
 * To stdout/stderr
 * To topics
 */

/* Give option of logging timestamp.
 * Logging pid.
 */
static int log_destinations = MQTT3_LOG_STDERR;
static int log_priorities = MOSQ_LOG_ERR | MOSQ_LOG_WARNING | MOSQ_LOG_NOTICE | MOSQ_LOG_INFO;

int mqtt3_log_init(int priorities, int destinations)
{
	int rc = 0;

	log_priorities = priorities;
	log_destinations = destinations;

	if(log_destinations & MQTT3_LOG_SYSLOG){
		openlog("mosquitto", LOG_PID, LOG_DAEMON);
	}

	return rc;
}

int mqtt3_log_close(void)
{
	if(log_destinations & MQTT3_LOG_SYSLOG){
		closelog();
	}
	/* FIXME - do something for all destinations! */

	return MOSQ_ERR_SUCCESS;
}

int mqtt3_log_printf(int priority, const char *fmt, ...)
{
	va_list va;
	char s[500];
	const char *topic;
	int syslog_priority;

	if((log_priorities & priority) && log_destinations != MQTT3_LOG_NONE){
		switch(priority){
			case MOSQ_LOG_DEBUG:
				topic = "$SYS/broker/log/D";
				syslog_priority = LOG_DEBUG;
				break;
			case MOSQ_LOG_ERR:
				topic = "$SYS/broker/log/E";
				syslog_priority = LOG_ERR;
				break;
			case MOSQ_LOG_WARNING:
				topic = "$SYS/broker/log/W";
				syslog_priority = LOG_WARNING;
				break;
			case MOSQ_LOG_NOTICE:
				topic = "$SYS/broker/log/N";
				syslog_priority = LOG_NOTICE;
				break;
			case MOSQ_LOG_INFO:
				topic = "$SYS/broker/log/I";
				syslog_priority = LOG_INFO;
				break;
			default:
				topic = "$SYS/broker/log/E";
				syslog_priority = LOG_ERR;
		}
		va_start(va, fmt);
		vsnprintf(s, 500, fmt, va);
		va_end(va);

		if(log_destinations & MQTT3_LOG_STDOUT){
			fprintf(stdout, "%s\n", s);
			fflush(stdout);
		}
		if(log_destinations & MQTT3_LOG_STDERR){
			fprintf(stderr, "%s\n", s);
			fflush(stderr);
		}
		if(log_destinations & MQTT3_LOG_SYSLOG){
			syslog(syslog_priority, "%s", s);
		}
		if(log_destinations & MQTT3_LOG_TOPIC && priority != MOSQ_LOG_DEBUG){
			mqtt3_db_messages_easy_queue(&int_db, NULL, topic, 2, strlen(s), (uint8_t *)s, 0);
		}
	}

	return MOSQ_ERR_SUCCESS;
}

