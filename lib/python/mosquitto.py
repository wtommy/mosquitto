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

class mosquitto_message(Structure):
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

libmosquitto = cdll.LoadLibrary("./libmosquitto.so.0")

mosquitto_new = libmosquitto.mosquitto_new
mosquitto_new.argtypes = [c_void_p, c_char_p]
mosquitto_new.restype = c_void_p

mosquitto_destroy = libmosquitto.mosquitto_destroy
mosquitto_destroy.argtypes = [c_void_p]
mosquitto_destroy.restype = None

mosquitto_connect = libmosquitto.mosquitto_connect
mosquitto_connect.argtypes = [c_void_p, c_char_p, c_int, c_int, c_bool]
mosquitto_connect.restype = c_int

mosquitto_disconnect = libmosquitto.mosquitto_disconnect
mosquitto_disconnect.argtypes = [c_void_p]
mosquitto_disconnect.restype = c_int

mosquitto_publish = libmosquitto.mosquitto_publish
mosquitto_publish.argtypes = [c_void_p, POINTER(c_uint16), c_char_p, c_uint32, POINTER(c_uint8), c_int, c_bool]
mosquitto_publish.restype = c_int

mosquitto_subscribe = libmosquitto.mosquitto_subscribe
mosquitto_subscribe.argtypes = [c_void_p, c_char_p, c_int]
mosquitto_subscribe.restype = c_int

mosquitto_unsubscribe = libmosquitto.mosquitto_unsubscribe
mosquitto_unsubscribe.argtypes = [c_void_p, c_char_p]
mosquitto_unsubscribe.restype = c_int

mosquitto_loop = libmosquitto.mosquitto_loop
mosquitto_loop.argtypes = [c_void_p, c_void_p]
mosquitto_loop.restype = c_int

mosquitto_message_cleanup = libmosquitto.mosquitto_message_cleanup
mosquitto_message_cleanup.argtypes = [POINTER(c_void_p)]
mosquitto_message_cleanup.restype = c_int

mosquitto_will_set = libmosquitto.mosquitto_will_set
mosquitto_will_set.argtypes = [c_void_p, c_bool, c_char_p, c_uint32, POINTER(c_uint8), c_int, c_bool]
mosquitto_will_set.restype = c_int

mosquitto_log_init = libmosquitto.mosquitto_log_init
mosquitto_log_init.argtypes = [c_void_p, c_int, c_int]
mosquitto_log_init.restype = c_int

mosquitto_connect_callback_set = libmosquitto.mosquitto_connect_callback_set
#mosquitto_connect_callback_set.argtypes = [c_void_p, c_void_p]
mosquitto_connect_callback_set.restype = None

mosquitto_message_callback_set = libmosquitto.mosquitto_message_callback_set
#mosquitto_message_callback_set.argtypes = [c_void_p, c_void_p]
mosquitto_message_callback_set.restype = None

MOSQ_CONNECT_FUNC = CFUNCTYPE(None, c_void_p, c_int)
MOSQ_PUBLISH_FUNC = CFUNCTYPE(None, c_void_p, c_uint16)
MOSQ_MESSAGE_FUNC = CFUNCTYPE(None, c_void_p, POINTER(mosquitto_message))
MOSQ_SUBSCRIBE_FUNC = CFUNCTYPE(None, c_void_p, c_uint16, c_int, POINTER(c_uint8))
MOSQ_UNSUBSCRIBE_FUNC = CFUNCTYPE(None, c_void_p, c_uint16)

def py_on_connect(obj, rc):
	print "rc: ", rc
on_connect = MOSQ_CONNECT_FUNC(py_on_connect)

def py_on_message(obj, message):
	print message.contents.topic
	#print message.next,message.timestamp,message.direction,message.state,message.mid,message.topic,message.qos
on_message = MOSQ_MESSAGE_FUNC(py_on_message)

#------------------------------------
mosq = mosquitto_new(0, "python")
mosquitto_connect_callback_set(mosq, on_connect)
mosquitto_message_callback_set(mosq, on_message)
#mosquitto_log_init(mosq, 0xFF, 0xFF)
mosquitto_connect(mosq, "127.0.0.1", 1883, 6, True)
mosquitto_subscribe(mosq, "$SYS/#", 2)
while 1:
	mosquitto_loop(mosq, 0)
mosquitto_destroy(mosq)

