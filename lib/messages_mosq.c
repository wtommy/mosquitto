/*
Copyright (c) 2010, Roger Light <roger@atchoo.org>
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

#include <mosquitto_internal.h>
#include <mosquitto.h>
#include <memory_mosq.h>
#include <messages_mosq.h>
#include <send_mosq.h>

#include <stdlib.h>

void _mosquitto_message_cleanup(struct mosquitto_message_all **message)
{
	struct mosquitto_message_all *msg;

	if(!message || !*message) return;

	msg = *message;

	if(msg->msg.topic) _mosquitto_free(msg->msg.topic);
	if(msg->msg.payload) _mosquitto_free(msg->msg.payload);
	_mosquitto_free(msg);
};

void _mosquitto_message_cleanup_all(struct mosquitto *mosq)
{
	struct mosquitto_message_all *tmp;

	if(!mosq) return;

	while(mosq->messages){
		tmp = mosq->messages->next;
		_mosquitto_message_cleanup(&mosq->messages);
		mosq->messages = tmp;
	}
};

int _mosquitto_message_delete(struct mosquitto *mosq, uint16_t mid, enum mosquitto_msg_direction dir)
{
	struct mosquitto_message_all *message, *prev = NULL;
	if(!mosq) return 1;

	message = mosq->messages;
	while(message){
		if(message->msg.mid == mid && message->direction == dir){
			if(prev){
				prev->next = message->next;
			}else{
				mosq->messages = message->next;
			}
			_mosquitto_message_cleanup(&message);
		}
		prev = message;
		message = message->next;
	}
	return 1;
}

int _mosquitto_message_queue(struct mosquitto *mosq, struct mosquitto_message_all *message)
{
	struct mosquitto_message_all *tail;

	if(!mosq || !message) return 1;

	message->next = NULL;
	if(mosq->messages){
		tail = mosq->messages;
		while(tail->next){
			tail = tail->next;
		}
		tail->next = message;
	}else{
		mosq->messages = message;
	}
	return 0;
}

int _mosquitto_message_remove(struct mosquitto *mosq, uint16_t mid, enum mosquitto_msg_direction dir, struct mosquitto_message_all **message)
{
	struct mosquitto_message_all *cur, *prev = NULL;
	if(!mosq || !message) return 1;

	cur = mosq->messages;
	while(cur){
		if(cur->msg.mid == mid && cur->direction == dir){
			if(prev){
				prev->next = cur->next;
			}else{
				mosq->messages = cur->next;
			}
			*message = cur;
			return 0;
		}
		prev = cur;
		cur = cur->next;
	}
	return 1;
}

void _mosquitto_message_retry_check(struct mosquitto *mosq)
{
	struct mosquitto_message_all *message;
	time_t now = time(NULL);
	if(!mosq) return;

	message = mosq->messages;
	while(message){
		if(message->timestamp + mosq->message_retry > now){
			switch(message->state){
				case mosq_ms_wait_puback:
				case mosq_ms_wait_pubrec:
					message->timestamp = now;
					message->dup = true;
					_mosquitto_send_publish(mosq, message->msg.mid, message->msg.topic, message->msg.payloadlen, message->msg.payload, message->msg.qos, message->msg.retain, message->dup);
					break;
				case mosq_ms_wait_pubrel:
					message->timestamp = now;
					_mosquitto_send_pubrec(mosq, message->msg.mid);
					break;
				case mosq_ms_wait_pubcomp:
					message->timestamp = now;
					_mosquitto_send_pubrel(mosq, message->msg.mid);
					break;
				default:
					break;
			}
		}
		message = message->next;
	}
}

void mosquitto_message_retry_set(struct mosquitto *mosq, unsigned int message_retry)
{
	if(mosq) mosq->message_retry = message_retry;
}

int _mosquitto_message_update(struct mosquitto *mosq, uint16_t mid, enum mosquitto_msg_direction dir, enum mosquitto_msg_state state)
{
	struct mosquitto_message_all *message;
	if(!mosq) return 1;

	message = mosq->messages;
	while(message){
		if(message->msg.mid == mid && message->direction == dir){
			message->state = state;
			message->timestamp = time(NULL);
			return 0;
		}
	}
	return 1;
}

