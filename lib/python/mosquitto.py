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

class Mosquitto:
	"""MQTT version 3 client class"""

	def __init__(self, id):
		#==================================================
		# Library loading
		#==================================================
		self.libmosq = cdll.LoadLibrary("./libmosquitto.so.0")
		self.mosquitto_new = self.libmosq.mosquitto_new
		self.mosquitto_new.argtypes = [c_void_p, c_char_p]
		self.mosquitto_new.restype = c_void_p

		self.mosquitto_destroy = self.libmosq.mosquitto_destroy
		self.mosquitto_destroy.argtypes = [c_void_p]
		self.mosquitto_destroy.restype = None

		self.mosquitto_connect = self.libmosq.mosquitto_connect
		self.mosquitto_connect.argtypes = [c_void_p, c_char_p, c_int, c_int, c_bool]
		self.mosquitto_connect.restype = c_int

		self.mosquitto_disconnect = self.libmosq.mosquitto_disconnect
		self.mosquitto_disconnect.argtypes = [c_void_p]
		self.mosquitto_disconnect.restype = c_int

		self.mosquitto_publish = self.libmosq.mosquitto_publish
		self.mosquitto_publish.argtypes = [c_void_p, POINTER(c_uint16), c_char_p, c_uint32, POINTER(c_uint8), c_int, c_bool]
		self.mosquitto_publish.restype = c_int

		self.mosquitto_subscribe = self.libmosq.mosquitto_subscribe
		self.mosquitto_subscribe.argtypes = [c_void_p, c_char_p, c_int]
		self.mosquitto_subscribe.restype = c_int

		self.mosquitto_unsubscribe = self.libmosq.mosquitto_unsubscribe
		self.mosquitto_unsubscribe.argtypes = [c_void_p, c_char_p]
		self.mosquitto_unsubscribe.restype = c_int

		self.mosquitto_loop = self.libmosq.mosquitto_loop
		self.mosquitto_loop.argtypes = [c_void_p, c_void_p]
		self.mosquitto_loop.restype = c_int

		self.mosquitto_message_cleanup = self.libmosq.mosquitto_message_cleanup
		self.mosquitto_message_cleanup.argtypes = [POINTER(c_void_p)]
		self.mosquitto_message_cleanup.restype = c_int

		self.mosquitto_will_set = self.libmosq.mosquitto_will_set
		self.mosquitto_will_set.argtypes = [c_void_p, c_bool, c_char_p, c_uint32, POINTER(c_uint8), c_int, c_bool]
		self.mosquitto_will_set.restype = c_int

		self.mosquitto_log_init = self.libmosq.mosquitto_log_init
		self.mosquitto_log_init.argtypes = [c_void_p, c_int, c_int]
		self.mosquitto_log_init.restype = c_int

		self.mosquitto_connect_callback_set = self.libmosq.mosquitto_connect_callback_set
		#self.mosquitto_connect_callback_set.argtypes = [c_void_p, c_void_p]
		self.mosquitto_connect_callback_set.restype = None

		self.mosquitto_message_callback_set = self.libmosq.mosquitto_message_callback_set
		#self.mosquitto_message_callback_set.argtypes = [c_void_p, c_void_p]
		self.mosquitto_message_callback_set.restype = None

		self.MOSQ_CONNECT_FUNC = CFUNCTYPE(None, c_void_p, c_int)
		self.MOSQ_PUBLISH_FUNC = CFUNCTYPE(None, c_void_p, c_uint16)
		self.MOSQ_MESSAGE_FUNC = CFUNCTYPE(None, c_void_p, POINTER(MosquittoMessage))
		self.MOSQ_SUBSCRIBE_FUNC = CFUNCTYPE(None, c_void_p, c_uint16, c_int, POINTER(c_uint8))
		self.MOSQ_UNSUBSCRIBE_FUNC = CFUNCTYPE(None, c_void_p, c_uint16)
		#==================================================
		# End library loading
		#==================================================
		self.mosq = self.mosquitto_new(0, id)

	def __del__(self):
		if self.mosq:
			self.mosquitto_destroy(pointer(self.mosq))

	def connect(self, hostname, port, keepalive, clean_session):
		return self.mosquitto_connect(self.mosq, hostname, port, keepalive, clean_session)

	def disconnect(self):
		return self.mosquitto_disconnect(self.mosq)

	def log_init(self, priorities, destinations):
		return self.mosquitto_log_init(self.mosq, priorities, destinations)

	def loop(self, timeout):
		return self.mosquitto_loop(self.mosq, 0)

	def subscribe(self, sub, qos):
		return self.mosquitto_subscribe(self.mosq, sub, qos)

	def unsubscribe(self, sub):
		return self.mosquitto_unsubscribe(self.mosq, sub)

	def publish(self, topic, payloadlen, payload, qos, retain):
		return self.mosquitto_publish(self.mosq, byref(mid), topic, payloadlen, payload, qos, retain)

	def will_set(self, will, topic, payloadlen, payload, qos, retain):
		return self.mosquitto_will_set(self.mosq, will, topic, payloadlen, payload, qos, retain)

	def connect_callback(self, callback):
		self.on_connect = self.MOSQ_CONNECT_FUNC(callback)
		return self.mosquitto_connect_callback_set(self.mosq, self.on_connect)
	
	def publish_callback(self, callback):
		self.on_publish = self.MOSQ_PUBLISH_FUNC(callback)
		return self.mosquitto_publish_callback_set(self.mosq, self.on_publish)
	
	def message_callback(self, callback):
		self.on_message = self.MOSQ_MESSAGE_FUNC(callback)
		return self.mosquitto_message_callback_set(self.mosq, self.on_message)
	
	def subscribe_callback(self, callback):
		self.on_subscribe = self.MOSQ_SUBSCRIBE_FUNC(callback)
		return self.mosquitto_subscribe_callback_set(self.mosq, self.on_subscribe)
	
	def unsubscribe_callback(self, callback):
		self.on_unsubscribe = self.MOSQ_UNSUBSCRIBE_FUNC(callback)
		return self.mosquitto_unsubscribe_callback_set(self.mosq, self.on_unsubscribe)
	

class MosquittoMessage(Structure):
	_fields_ = [("next", c_void_p),
                ("timestamp", c_int),
				("direction", c_int),
				("state", c_int),
				("mid", c_uint16),
				("topic", c_char_p),
				("payload", POINTER(c_uint8)),
				("payloadlen", c_uint32),
				("qos", c_int),
				("retain", c_bool),
				("dup", c_bool)]


def py_on_connect(obj, rc):
	print "rc: ", rc


def py_on_message(obj, message):
	print message.contents.topic


bob = Mosquitto("bob")
bob.connect_callback(py_on_connect)
bob.message_callback(py_on_message)
bob.connect("127.0.0.1", 1883, 60, True)
bob.subscribe("$SYS/#", 2)

while 1:
	bob.loop(None)

