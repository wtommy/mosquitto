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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <mosquitto.h>
#include <logging_mosq.h>
#include <memory_mosq.h>
#include <messages_mosq.h>
#include <mqtt3_protocol.h>
#include <net_mosq.h>
#include <read_handle.h>
#include <send_mosq.h>
#include <util_mosq.h>

int _mosquitto_packet_handle(struct mosquitto *mosq)
{
	assert(mosq);

	switch((mosq->core.in_packet.command)&0xF0){
		case PINGREQ:
			return _mosquitto_handle_pingreq(mosq);
		case PINGRESP:
			return _mosquitto_handle_pingresp(mosq);
		case PUBACK:
		case PUBCOMP:
			return _mosquitto_handle_pubackcomp(mosq);
		case PUBLISH:
			return _mosquitto_handle_publish(mosq);
		case PUBREC:
			return _mosquitto_handle_pubrec(mosq);
		case PUBREL:
			return _mosquitto_handle_pubrel(mosq);
		case CONNACK:
			return _mosquitto_handle_connack(mosq);
		case SUBACK:
			return _mosquitto_handle_suback(mosq);
		case UNSUBACK:
			return _mosquitto_handle_unsuback(mosq);
		default:
			/* If we don't recognise the command, return an error straight away. */
			_mosquitto_log_printf(mosq, MOSQ_LOG_ERR, "Error: Unrecognised command %d\n", (mosq->core.in_packet.command)&0xF0);
			return MOSQ_ERR_PROTOCOL;
	}
}

int _mosquitto_handle_pingreq(struct mosquitto *mosq)
{
	assert(mosq);
	if(mosq->core.in_packet.remaining_length != 0){
		return MOSQ_ERR_PROTOCOL;
	}
	_mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Received PINGREQ");
	return _mosquitto_send_pingresp(mosq);
}

int _mosquitto_handle_pingresp(struct mosquitto *mosq)
{
	assert(mosq);
	if(mosq->core.in_packet.remaining_length != 0){
		return MOSQ_ERR_PROTOCOL;
	}
	_mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Received PINGRESP");
	return MOSQ_ERR_SUCCESS;
}

int _mosquitto_handle_pubackcomp(struct mosquitto *mosq)
{
	uint16_t mid;
	int rc;

	assert(mosq);
	if(mosq->core.in_packet.remaining_length != 2){
		return MOSQ_ERR_PROTOCOL;
	}
	rc = _mosquitto_read_uint16(&mosq->core.in_packet, &mid);
	if(rc) return rc;
	_mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Received PUBACK/PUBCOMP (Mid: %d)", mid);

	if(!_mosquitto_message_delete(mosq, mid, mosq_md_out)){
		/* Only inform the client the message has been sent once. */
		if(mosq->on_publish){
			mosq->on_publish(mosq->obj, mid);
		}
	}

	return MOSQ_ERR_SUCCESS;
}

int _mosquitto_handle_publish(struct mosquitto *mosq)
{
	uint8_t header;
	struct mosquitto_message_all *message;
	int rc = 0;

	assert(mosq);

	message = _mosquitto_calloc(1, sizeof(struct mosquitto_message_all));
	if(!message) return MOSQ_ERR_NOMEM;

	header = mosq->core.in_packet.command;

	_mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Received PUBLISH");
	message->direction = mosq_md_in;
	message->dup = (header & 0x08)>>3;
	message->msg.qos = (header & 0x06)>>1;
	message->msg.retain = (header & 0x01);

	rc = _mosquitto_read_string(&mosq->core.in_packet, &message->msg.topic);
	if(rc) return rc;
	if(_mosquitto_fix_sub_topic(&message->msg.topic)) return 1;
	if(!strlen(message->msg.topic)){
		_mosquitto_message_cleanup(&message);
		return 1;
	}

	if(message->msg.qos > 0){
		rc = _mosquitto_read_uint16(&mosq->core.in_packet, &message->msg.mid);
		if(rc){
			_mosquitto_message_cleanup(&message);
			return rc;
		}
	}

	message->msg.payloadlen = mosq->core.in_packet.remaining_length - mosq->core.in_packet.pos;
	if(message->msg.payloadlen){
		message->msg.payload = _mosquitto_calloc(message->msg.payloadlen+1, sizeof(uint8_t));
		_mosquitto_read_bytes(&mosq->core.in_packet, message->msg.payload, message->msg.payloadlen);
		if(rc){
			_mosquitto_message_cleanup(&message);
			return rc;
		}
	}

	message->timestamp = time(NULL);
	switch(message->msg.qos){
		case 0:
			if(mosq->on_message){
				mosq->on_message(mosq->obj, &message->msg);
			}
			_mosquitto_message_cleanup(&message);
			return MOSQ_ERR_SUCCESS;
		case 1:
			rc = _mosquitto_send_puback(mosq, message->msg.mid);
			if(mosq->on_message){
				mosq->on_message(mosq->obj, &message->msg);
			}
			_mosquitto_message_cleanup(&message);
			return rc;
		case 2:
			rc = _mosquitto_send_pubrec(mosq, message->msg.mid);
			message->state = mosq_ms_wait_pubrel;
			_mosquitto_message_queue(mosq, message);
			return rc;
		default:
			return MOSQ_ERR_PROTOCOL;
	}
}

int _mosquitto_handle_pubrec(struct mosquitto *mosq)
{
	uint16_t mid;
	int rc;

	assert(mosq);
	if(mosq->core.in_packet.remaining_length != 2){
		return MOSQ_ERR_PROTOCOL;
	}
	rc = _mosquitto_read_uint16(&mosq->core.in_packet, &mid);
	if(rc) return rc;
	_mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Received PUBREC (Mid: %d)", mid);

	rc = _mosquitto_message_update(mosq, mid, mosq_md_out, mosq_ms_wait_pubcomp);
	if(rc) return rc;
	rc = _mosquitto_send_pubrel(mosq, mid);
	if(rc) return rc;

	return MOSQ_ERR_SUCCESS;
}

int _mosquitto_handle_pubrel(struct mosquitto *mosq)
{
	uint16_t mid;
	struct mosquitto_message_all *message = NULL;
	int rc;

	assert(mosq);
	if(mosq->core.in_packet.remaining_length != 2){
		return MOSQ_ERR_PROTOCOL;
	}
	rc = _mosquitto_read_uint16(&mosq->core.in_packet, &mid);
	if(rc) return rc;
	_mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Received PUBREL (Mid: %d)", mid);

	if(!_mosquitto_message_remove(mosq, mid, mosq_md_in, &message)){
		/* Only pass the message on if we have removed it from the queue - this
		 * prevents multiple callbacks for the same message. */
		if(mosq->on_message){
			mosq->on_message(mosq->obj, &message->msg);
		}else{
			_mosquitto_message_cleanup(&message);
		}
	}
	rc = _mosquitto_send_pubcomp(mosq, mid);
	if(rc) return rc;

	return MOSQ_ERR_SUCCESS;
}
