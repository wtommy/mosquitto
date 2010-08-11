#!/usr/bin/python

# Copyright (c) 2010, Roger Light <roger@atchoo.org>
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
# 1. Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
# 3. Neither the name of mosquitto nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

from ctypes import *
from ctypes.util import find_library

# Log destinations
MOSQ_LOG_NONE=0x00
MOSQ_LOG_STDOUT=0x04
MOSQ_LOG_STDERR=0x08

# Log types
MOSQ_LOG_INFO=0x01
MOSQ_LOG_NOTICE=0x02
MOSQ_LOG_WARNING=0x04
MOSQ_LOG_ERR=0x08
MOSQ_LOG_DEBUG=0x10

class Mosquitto:
	"""MQTT version 3 client class"""

	def __init__(self, id, obj=None):
		#==================================================
		# Library loading
		#==================================================
		self._libmosq = cdll.LoadLibrary(find_library("mosquitto"))
		self._mosquitto_new = self._libmosq.mosquitto_new
		self._mosquitto_new.argtypes = [c_char_p, c_void_p]
		self._mosquitto_new.restype = c_void_p

		self._mosquitto_destroy = self._libmosq.mosquitto_destroy
		self._mosquitto_destroy.argtypes = [c_void_p]
		self._mosquitto_destroy.restype = None

		self._mosquitto_connect = self._libmosq.mosquitto_connect
		self._mosquitto_connect.argtypes = [c_void_p, c_char_p, c_int, c_int, c_bool]
		self._mosquitto_connect.restype = c_int

		self._mosquitto_disconnect = self._libmosq.mosquitto_disconnect
		self._mosquitto_disconnect.argtypes = [c_void_p]
		self._mosquitto_disconnect.restype = c_int

		self._mosquitto_publish = self._libmosq.mosquitto_publish
		self._mosquitto_publish.argtypes = [c_void_p, POINTER(c_uint16), c_char_p, c_uint32, POINTER(c_uint8), c_int, c_bool]
		self._mosquitto_publish.restype = c_int

		self._mosquitto_subscribe = self._libmosq.mosquitto_subscribe
		self._mosquitto_subscribe.argtypes = [c_void_p, POINTER(c_uint16), c_char_p, c_int]
		self._mosquitto_subscribe.restype = c_int

		self._mosquitto_unsubscribe = self._libmosq.mosquitto_unsubscribe
		self._mosquitto_unsubscribe.argtypes = [c_void_p, POINTER(c_uint16), c_char_p]
		self._mosquitto_unsubscribe.restype = c_int

		self._mosquitto_loop = self._libmosq.mosquitto_loop
		self._mosquitto_loop.argtypes = [c_void_p, c_int]
		self._mosquitto_loop.restype = c_int

		self._mosquitto_will_set = self._libmosq.mosquitto_will_set
		self._mosquitto_will_set.argtypes = [c_void_p, c_bool, c_char_p, c_uint32, POINTER(c_uint8), c_int, c_bool]
		self._mosquitto_will_set.restype = c_int

		self._mosquitto_log_init = self._libmosq.mosquitto_log_init
		self._mosquitto_log_init.argtypes = [c_void_p, c_int, c_int]
		self._mosquitto_log_init.restype = c_int

		self._mosquitto_connect_callback_set = self._libmosq.mosquitto_connect_callback_set
		#self._mosquitto_connect_callback_set.argtypes = [c_void_p, c_void_p]
		self._mosquitto_connect_callback_set.restype = None

		self._mosquitto_disconnect_callback_set = self._libmosq.mosquitto_disconnect_callback_set
		#self._mosquitto_disconnect_callback_set.argtypes = [c_void_p, c_void_p]
		self._mosquitto_disconnect_callback_set.restype = None

		self._mosquitto_publish_callback_set = self._libmosq.mosquitto_publish_callback_set
		#self._mosquitto_publish_callback_set.argtypes = [c_void_p, c_void_p]
		self._mosquitto_publish_callback_set.restype = None

		self._mosquitto_message_callback_set = self._libmosq.mosquitto_message_callback_set
		#self._mosquitto_message_callback_set.argtypes = [c_void_p, c_void_p]
		self._mosquitto_message_callback_set.restype = None

		self._mosquitto_subscribe_callback_set = self._libmosq.mosquitto_subscribe_callback_set
		#self._mosquitto_subscribe_callback_set.argtypes = [c_void_p, c_void_p]
		self._mosquitto_subscribe_callback_set.restype = None

		self._mosquitto_unsubscribe_callback_set = self._libmosq.mosquitto_unsubscribe_callback_set
		#self._mosquitto_unsubscribe_callback_set.argtypes = [c_void_p, c_void_p]
		self._mosquitto_unsubscribe_callback_set.restype = None

		self._MOSQ_CONNECT_FUNC = CFUNCTYPE(None, c_void_p, c_int)
		self._MOSQ_DISCONNECT_FUNC = CFUNCTYPE(None, c_void_p)
		self._MOSQ_PUBLISH_FUNC = CFUNCTYPE(None, c_void_p, c_uint16)
		self._MOSQ_MESSAGE_FUNC = CFUNCTYPE(None, c_void_p, POINTER(MosquittoMessage))
		self._MOSQ_SUBSCRIBE_FUNC = CFUNCTYPE(None, c_void_p, c_uint16, c_int, POINTER(c_uint8))
		self._MOSQ_UNSUBSCRIBE_FUNC = CFUNCTYPE(None, c_void_p, c_uint16)
		#==================================================
		# End library loading
		#==================================================
		
		self._mosq = self._mosquitto_new(id, obj)

		#==================================================
		# Configure callbacks
		#==================================================
	def connect_callback(self, callback):
		self._internal_on_connect_cast = self._MOSQ_CONNECT_FUNC(self._internal_on_connect)
		self._mosquitto_connect_callback_set(self._mosq, self._internal_on_connect_cast)
		self._on_connect = None
	
		self._internal_on_message_cast = self._MOSQ_MESSAGE_FUNC(self._internal_on_message)
		self._mosquitto_message_callback_set(self._mosq, self._internal_on_message_cast)
		self.on_message = None
		#==================================================
		# End configure callbacks
		#==================================================

	def __del__(self):
		if self._mosq:
			self._mosquitto_destroy(self._mosq)

	def connect(self, hostname="127.0.0.1", port=1883, keepalive=60, clean_session=True):
		return self._mosquitto_connect(self._mosq, hostname, port, keepalive, clean_session)

	def disconnect(self):
		return self._mosquitto_disconnect(self._mosq)

	def log_init(self, priorities, destinations):
		return self._mosquitto_log_init(self._mosq, priorities, destinations)

	def loop(self, timeout=0):
		return self._mosquitto_loop(self._mosq, timeout)

	def subscribe(self, sub, qos):
		return self._mosquitto_subscribe(self._mosq, None, sub, qos)

	def unsubscribe(self, sub):
		return self._mosquitto_unsubscribe(self._mosq, None, sub)

	def publish(self, topic, payloadlen, payload, qos=0, retain=False):
		return self._mosquitto_publish(self._mosq, None, topic, payloadlen, payload, qos, retain)

	def will_set(self, will, topic, payloadlen, payload, qos=0, retain=False):
		return self._mosquitto_will_set(self._mosq, will, topic, payloadlen, payload, qos, retain)

	def _internal_on_connect(self, obj, rc):
		if self.on_connect:
			self.on_connect(rc)

	def _internal_on_message(self, obj, message):
		if self.on_message:
			topic = message.contents.topic
			payload = message.contents.payload
			qos = message.contents.qos
			retain = message.contents.retain
			self.on_message(topic, payload, qos, retain)

	def disconnect_callback(self, callback):
		self._on_disconnect = self._MOSQ_DISCONNECT_FUNC(callback)
		return self._mosquitto_disconnect_callback_set(self._mosq, self._on_disconnect)
	
	def publish_callback(self, callback):
		self._on_publish = self._MOSQ_PUBLISH_FUNC(callback)
		return self._mosquitto_publish_callback_set(self._mosq, self._on_publish)
	
	def subscribe_callback(self, callback):
		self._on_subscribe = self._MOSQ_SUBSCRIBE_FUNC(callback)
		return self._mosquitto_subscribe_callback_set(self._mosq, self._on_subscribe)
	
	def unsubscribe_callback(self, callback):
		self._on_unsubscribe = self._MOSQ_UNSUBSCRIBE_FUNC(callback)
		return self._mosquitto_unsubscribe_callback_set(self._mosq, self._on_unsubscribe)

class MosquittoMessage(Structure):
	_fields_ = [("mid", c_uint16),
				("topic", c_char_p),
				("payload", c_char_p),
				("payloadlen", c_uint32),
				("qos", c_int),
				("retain", c_bool)]

