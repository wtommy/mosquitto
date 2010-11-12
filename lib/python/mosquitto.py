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
	"""MQTT version 3.1 client class.
	
	This is the main class for use communicating with an MQTT broker.

	General usage flow:

	* Use connect() to connect to a broker
	* Call loop() frequently to maintain network traffic flow with the broker
	* Use subscribe() to subscribe to a topic and receive messages
	* Use publish() to send messages
	* Use disconnect() to disconnect from the broker

	Data returned from the broker is made available with the use of callback
	functions as described below.

	Callbacks
	=========

	A number of callback functions are available to receive data back from the
	broker. To use a callback, define a function and then assign it to the
	client. The callback function may be a class member if desired. All of the
	callbacks as described below have an "obj" argument. This variable
	corresponds directly to the obj argument passed when creating the client
	instance. It is however optional when defining the callback functions, so
	the on connect callback function can be defined as on_connect(obj, rc) or
	on_connect(rc) for example.

	The callbacks:

	on_connect(obj, rc): called when the broker responds to our connection
	  request. The value of rc determines success or not:
	  0: Connection successful
	  1: Connection refused - incorrect protocol version
	  2: Connection refused - invalid client identifier
	  3: Connection refused - server unavailable
	  4: Connection refused - bad username or password
	  5: Connection refused - not authorised
	  6-255: Currently unused.

	on_disconnect(obj): called when the client disconnects from the broker, but
	  only after having sent a disconnection message to the broker. This will
	  not be called if the client is disconnected unexpectedly.

	on_message(obj, message): called when a message has been received on a
	  topic that the client subscribes to. The message variable is a
	  MosquittoMessage that describs all of the message parameters.

	on_publish(obj, mid): called when a message that was to be sent using the
	  publish() call has completed transmission to the broker. For messages
	  with QoS levels 1 and 2, this means that the appropriate handshakes have
	  completed. For QoS 0, this simply means that the message has left the
	  client. The mid variable matches the mid variable returned from the
	  corresponding publish() call, to allow outgoing messages to be tracked.
	  This callback is important because even if the publish() call returns
	  success, it does not means that the message has been sent.

	on_subscribe(obj, mid, granted_qos): called when the broker responds to a
	  subscribe request. The mid variable matches the mid variable returned
	  from the corresponding subscribe() call. The granted_qos variable is a
	  list of integers that give the QoS level the broker has granted for each
	  of the different subscription requests.

	on_unsubscribe(obj, mid): called when the broker responds to an unsubscribe
	  request. The mid variable matches the mid variable returned from the
	  corresponding unsubscribe() call.

	Example:

	def on_connect(rc):
		if rc == 0:
			print "Connected ok"
	
	client = mosquitto.Mosquitto("id")
	client.on_connect = on_connect
	...

	"""

	def __init__(self, id, obj=None):
		"""Constructor

		id: The 23 character or less client id used to identify this client to
		  the broker. This must be unique on the broker.
		obj: An optional object of any type that will be passed to any of the
		  callback function when they are called. If not set, or set to None,
		  this instance of the Mosquitto class will be passed to the callback
		  functions.
		"""
		if obj==None:
			self.obj = self
		else:
			self.obj = obj

		self._mosq = _mosquitto_new(id, None)

		#==================================================
		# Configure callbacks
		#==================================================
		self._internal_on_connect_cast = _MOSQ_CONNECT_FUNC(self._internal_on_connect)
		_mosquitto_connect_callback_set(self._mosq, self._internal_on_connect_cast)
		self._on_connect = None
	
		self._internal_on_disconnect_cast = _MOSQ_DISCONNECT_FUNC(self._internal_on_disconnect)
		_mosquitto_disconnect_callback_set(self._mosq, self._internal_on_disconnect_cast)
		self._on_disconnect = None
	
		self._internal_on_message_cast = _MOSQ_MESSAGE_FUNC(self._internal_on_message)
		_mosquitto_message_callback_set(self._mosq, self._internal_on_message_cast)
		self.on_message = None

		self._internal_on_publish_cast = _MOSQ_PUBLISH_FUNC(self._internal_on_publish)
		_mosquitto_publish_callback_set(self._mosq, self._internal_on_publish_cast)
		self.on_publish = None
	
		self._internal_on_subscribe_cast = _MOSQ_SUBSCRIBE_FUNC(self._internal_on_subscribe)
		_mosquitto_subscribe_callback_set(self._mosq, self._internal_on_subscribe_cast)
		self.on_subscribe = None
	
		self._internal_on_unsubscribe_cast = _MOSQ_UNSUBSCRIBE_FUNC(self._internal_on_unsubscribe)
		_mosquitto_unsubscribe_callback_set(self._mosq, self._internal_on_unsubscribe_cast)
		self.on_unsubscribe = None
		#==================================================
		# End configure callbacks
		#==================================================

	def __del__(self):
		if self._mosq:
			_mosquitto_destroy(self._mosq)

	def connect(self, hostname="localhost", port=1883, keepalive=60, clean_session=True):
		"""Connect the client to an MQTT broker.
		
		hostname: The hostname or ip address of the broker. Defaults to localhost.
		port: The network port of the server host to connect to. Defaults to 1883.
		keepalive: Maximum period in seconds between communications with the
		  broker. If no other messages are being exchanged, this controls the
		  rate at which the client will send ping messages to the broker.
		clean_session: If set to True, the broker will clean any previous
		  information about this client on connection, and will also not store
		  anything after disconnect. If set to False, the broker will store all
		  of the client subscriptions even after the client disconnects, and
		  will also queue messages with QoS 1 and 2 until the client
		  reconnects.

		Returns 0 on success (note that this just means a network connection
		  has been established between the broker and client, and the
		  connection request sent. To monitor the success of the connection
		  request, use the on_connect() callback)
		Returns >0 on error.
		"""
		return _mosquitto_connect(self._mosq, hostname, port, keepalive, clean_session)

	def disconnect(self):
		"""Disconnect a connected client from the broker."""
		return _mosquitto_disconnect(self._mosq)

	def log_init(self, priorities, destinations):
		"""Set the logging preferences for this client.
		
		Set priorities to a logically OR'd combination of:

		MOSQ_LOG_INFO
		MOSQ_LOG_NOTICE
		MOSQ_LOG_WARNING
		MOSQ_LOG_ERR
		MOSQ_LOG_DEBUG
		
		Set destinations to either MOSQ_LOG_NONE or a logically OR'd
		combination of:

		MOSQ_LOG_STDOUT=0x04
		MOSQ_LOG_STDERR=0x08
		"""
		return _mosquitto_log_init(self._mosq, priorities, destinations)

	def loop(self, timeout=-1):
		"""Process network events.
		
		This function must be called regularly to ensure communication with the broker is carried out.
		
		timeout: The time in milliseconds to wait for incoming/outgoing network
		  traffic before timing out and returning. If set to -1 or not given,
		  the default value of 1000 (1 second) will be used.

		Returns 0 on success.
		Returns >0 on error."""

		return _mosquitto_loop(self._mosq, timeout)

	def subscribe(self, sub, qos=0):
		"""Subscribe the client to a topic.
		
		sub: The subscription topic to subscribe to.
		qos: The desired quality of service level for the subscription.

		Returns 0 on success.
		Returns >1 on error."""
		return _mosquitto_subscribe(self._mosq, None, sub, qos)

	def unsubscribe(self, sub):
		"""Unsubscribe the client from a topic.
		
		sub: The subscription topic to unsubscribe from.

		Returns 0 on success.
		Returns >1 on error."""
		return _mosquitto_unsubscribe(self._mosq, None, sub)

	def publish(self, topic, payload=None, qos=0, retain=False):
		"""Publish a message on a topic.
		
		This causes a message to be sent to the broker and subsequently from
		the broker to any clients subscribing to matching topics.
		
		topic: The topic that the message should be published on.
		payload: The actual message to send. If not given, or set to None a
		  zero length message will be used.
		qos: The quality of service level to use.
		retain: If set to true, the message will be set as the "last known
		  good"/retained message for the topic.

		Returns 0 on success.
		Returns >1 on error."""
		return _mosquitto_publish(self._mosq, None, topic, len(payload), cast(payload, POINTER(c_uint8)), qos, retain)

	def will_set(self, topic, payload=None, qos=0, retain=False):
		"""Set a Will to be sent by the broker in case the client disconnects unexpectedly.

		This must be called before connect() to have any effect.

		topic: The topic that the will message should be published on.
		payload: The message to send as a will. If not given, or set to None a
		  zero length message will be used as the will.
		qos: The quality of service level to use for the will.
		retain: If set to true, the will message will be set as the "last known
		  good"/retained message for the topic.

		Returns 0 on success.
		Returns >1 on error."""

		return _mosquitto_will_set(self._mosq, true, topic, len(payloadlen), cast(payload, POINTER(c_uint8)), qos, retain)

	def will_clear(self):
		"""Clear a Will that was previously set with the will_set() call.

		This must be called before connect() to have any effect."""
		return _mosquitto_will_set(self._mosq, false, "", 0, cast(None, POINTER(c_uint8)), 0, 0)

	def username_pw_set(self, username, password=None):
		"""Set a username and optionally a password for broker authentication.

		Must be called before connect() to have any effect.
		Requires a broker that supports MQTT v3.1.
		
		username: The username to authenticate with. Need have no relationship to the client id.
		password: The password to authenticate with. Optional.
		
		Returns 0 on success.
		Returns >0 on error."""
		return _mosquitto_username_pw_set(self._mosq, username, password)

	def _internal_on_connect(self, obj, rc):
		if self.on_connect:
			argcount = self.on_connect.func_code.co_argcount

			if argcount == 1:
				self.on_connect(rc)
			elif argcount == 2:
				self.on_connect(self.obj, rc)

	def _internal_on_disconnect(self, obj):
		if self.on_disconnect:
			argcount = self.on_disconnect.func_code.co_argcount

			if argcount == 0:
				self.on_disconnect()
			elif argcount == 1:
				self.on_disconnect(self.obj)

	def _internal_on_message(self, obj, message):
		if self.on_message:
			topic = message.contents.topic
			payload = message.contents.payload
			qos = message.contents.qos
			retain = message.contents.retain
			msg = MosquittoMessage(topic, payload, qos, retain)
			argcount = self.on_message.func_code.co_argcount

			if argcount == 1:
				self.on_message(msg)
			elif argcount == 2:
				self.on_message(self.obj, msg)

	def _internal_on_publish(self, obj, mid):
		if self.on_publish:
			argcount = self.on_publish.func_code.co_argcount

			if argcount == 1:
				self.on_publish(mid)
			elif argcount == 2:
				self.on_publish(self.obj, mid)

	def _internal_on_subscribe(self, obj, mid, qos_count, granted_qos):
		if self.on_subscribe:
			qos_list = []
			for i in range(qos_count):
				qos_list.append(granted_qos[i])
			argcount = self.on_subscribe.func_code.co_argcount

			if argcount == 2:
				self.on_subscribe(mid, qos_list)
			elif argcount == 3:
				self.on_subscribe(self.obj, mid, qos_list)

	def _internal_on_unsubscribe(self, obj, mid):
		if self.on_unsubscribe:
			argcount = self.on_unsubscribe.func_code.co_argcount

			if argcount == 1:
				self.on_unsubscribe(mid)
			elif argcount == 2:
				self.on_unsubscribe(self.obj, mid)

class c_MosquittoMessage(Structure):
	"""Internal message class used for communicating with C library.

	Don't use."""
	_fields_ = [("mid", c_uint16),
				("topic", c_char_p),
				("payload", c_char_p),
				("payloadlen", c_uint32),
				("qos", c_int),
				("retain", c_bool)]

class MosquittoMessage:
	"""MQTT message class"""
	def __init__(self, topic, payload, qos, retain):
		self.topic = topic
		self.payload = payload
		self.qos = qos
		self.retain = retain

#==================================================
# Library loading
#==================================================
_libmosq = cdll.LoadLibrary(find_library("mosquitto"))
_mosquitto_new = _libmosq.mosquitto_new
_mosquitto_new.argtypes = [c_char_p, c_void_p]
_mosquitto_new.restype = c_void_p

_mosquitto_destroy = _libmosq.mosquitto_destroy
_mosquitto_destroy.argtypes = [c_void_p]
_mosquitto_destroy.restype = None

_mosquitto_connect = _libmosq.mosquitto_connect
_mosquitto_connect.argtypes = [c_void_p, c_char_p, c_int, c_int, c_bool]
_mosquitto_connect.restype = c_int

_mosquitto_disconnect = _libmosq.mosquitto_disconnect
_mosquitto_disconnect.argtypes = [c_void_p]
_mosquitto_disconnect.restype = c_int

_mosquitto_publish = _libmosq.mosquitto_publish
_mosquitto_publish.argtypes = [c_void_p, POINTER(c_uint16), c_char_p, c_uint32, POINTER(c_uint8), c_int, c_bool]
_mosquitto_publish.restype = c_int

_mosquitto_subscribe = _libmosq.mosquitto_subscribe
_mosquitto_subscribe.argtypes = [c_void_p, POINTER(c_uint16), c_char_p, c_int]
_mosquitto_subscribe.restype = c_int

_mosquitto_unsubscribe = _libmosq.mosquitto_unsubscribe
_mosquitto_unsubscribe.argtypes = [c_void_p, POINTER(c_uint16), c_char_p]
_mosquitto_unsubscribe.restype = c_int

_mosquitto_loop = _libmosq.mosquitto_loop
_mosquitto_loop.argtypes = [c_void_p, c_int]
_mosquitto_loop.restype = c_int

_mosquitto_will_set = _libmosq.mosquitto_will_set
_mosquitto_will_set.argtypes = [c_void_p, c_bool, c_char_p, c_uint32, POINTER(c_uint8), c_int, c_bool]
_mosquitto_will_set.restype = c_int

_mosquitto_log_init = _libmosq.mosquitto_log_init
_mosquitto_log_init.argtypes = [c_void_p, c_int, c_int]
_mosquitto_log_init.restype = c_int

_mosquitto_connect_callback_set = _libmosq.mosquitto_connect_callback_set
_mosquitto_connect_callback_set.argtypes = [c_void_p, c_void_p]
_mosquitto_connect_callback_set.restype = None

_mosquitto_disconnect_callback_set = _libmosq.mosquitto_disconnect_callback_set
_mosquitto_disconnect_callback_set.argtypes = [c_void_p, c_void_p]
_mosquitto_disconnect_callback_set.restype = None

_mosquitto_publish_callback_set = _libmosq.mosquitto_publish_callback_set
_mosquitto_publish_callback_set.argtypes = [c_void_p, c_void_p]
_mosquitto_publish_callback_set.restype = None

_mosquitto_message_callback_set = _libmosq.mosquitto_message_callback_set
_mosquitto_message_callback_set.argtypes = [c_void_p, c_void_p]
_mosquitto_message_callback_set.restype = None

_mosquitto_subscribe_callback_set = _libmosq.mosquitto_subscribe_callback_set
_mosquitto_subscribe_callback_set.argtypes = [c_void_p, c_void_p]
_mosquitto_subscribe_callback_set.restype = None

_mosquitto_unsubscribe_callback_set = _libmosq.mosquitto_unsubscribe_callback_set
_mosquitto_unsubscribe_callback_set.argtypes = [c_void_p, c_void_p]
_mosquitto_unsubscribe_callback_set.restype = None

_MOSQ_CONNECT_FUNC = CFUNCTYPE(None, c_void_p, c_int)
_MOSQ_DISCONNECT_FUNC = CFUNCTYPE(None, c_void_p)
_MOSQ_PUBLISH_FUNC = CFUNCTYPE(None, c_void_p, c_uint16)
_MOSQ_MESSAGE_FUNC = CFUNCTYPE(None, c_void_p, POINTER(c_MosquittoMessage))
_MOSQ_SUBSCRIBE_FUNC = CFUNCTYPE(None, c_void_p, c_uint16, c_int, POINTER(c_uint8))
_MOSQ_UNSUBSCRIBE_FUNC = CFUNCTYPE(None, c_void_p, c_uint16)
#==================================================
# End library loading
#==================================================

