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

#ifndef _MOSQUITTO_H_
#define _MOSQUITTO_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#ifdef mosquitto_EXPORTS
#define mosq_EXPORT  __declspec(dllexport)
#else
#define mosq_EXPORT  __declspec(dllimport)
#endif
#else
#define mosq_EXPORT
#endif

#define LIBMOSQUITTO_MAJOR 0
#define LIBMOSQUITTO_MINOR 8
#define LIBMOSQUITTO_REVISION 1

#include <stdbool.h>
#include <stdint.h>

/* Log destinations */
#define MOSQ_LOG_NONE 0x00
#define MOSQ_LOG_STDOUT 0x04
#define MOSQ_LOG_STDERR 0x08

/* Log types */
#define MOSQ_LOG_INFO 0x01
#define MOSQ_LOG_NOTICE 0x02
#define MOSQ_LOG_WARNING 0x04
#define MOSQ_LOG_ERR 0x08
#define MOSQ_LOG_DEBUG 0x10

struct mosquitto_message{
	uint16_t mid;
	char *topic;
	uint8_t *payload;
	uint32_t payloadlen;
	int qos;
	bool retain;
};

struct mosquitto;

/***************************************************
 * Important note
 * 
 * The following functions that deal with network operations will return 0 on
 * success, but this does not mean that the operation has taken place. Rather,
 * the appropriate messages will have been queued and will be completed when
 * calling mosquitto_loop(). To be sure of the event having taken place, use
 * the callbacks. This is especially important when disconnecting a client that
 * has a will. If the broker does not receive the DISCONNECT command, it will
 * assume that the client has disconnected unexpectedly and send the will.
 *
 * mosquitto_connect()
 * mosquitto_disconnect()
 * mosquitto_subscribe()
 * mosquitto_unsubscribe()
 * mosquitto_publish()
 ***************************************************/

mosq_EXPORT void mosquitto_lib_version(int *major, int *minor, int *revision);
/* Return the version of the compiled library. */

mosq_EXPORT int mosquitto_lib_init(void);
/* Must be called before any other mosquitto functions.
 * Returns 0 on success, 1 on error.
 */

mosq_EXPORT int mosquitto_lib_cleanup(void);
/* Must be called before the program exits. */


mosq_EXPORT struct mosquitto *mosquitto_new(const char *id, void *obj);
/* Create a new mosquitto client instance.
 *
 * id :  String to use as the client id. Must not be NULL or zero length.
 * obj : A user pointer that will be passed as an argument to any callbacks
 *       that are specified.
 *
 * Returns a memory pointer on success, NULL on failure.
 */

mosq_EXPORT void mosquitto_destroy(struct mosquitto *mosq);
/* Free memory associated with a mosquitto client instance. */


mosq_EXPORT int mosquitto_log_init(struct mosquitto *mosq, int priorities, int destinations);
/* Configure logging options for a client instance. May be called at any point.
 *
 * mosq :         a valid mosquitto instance
 * priorities :   which logging levels to output. See "Log types" above. Combine
 *                multiple types with the OR operator |
 * destinations : where to log messages. See "Log destinations" above. Combine
 *                 multiple types with the OR operator |
 */

mosq_EXPORT int mosquitto_will_set(struct mosquitto *mosq, bool will, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain);
/* Configure will information for a mosquitto instance. By default, clients do not have a will.
 * This must be called before calling mosquitto_connect().
 *
 * mosq :       a valid mosquitto instance
 * will :       set to true to enable a will, false to disable. If set to true,
 *              at least "topic" but also be valid.
 * topic :      the topic to publish the will on
 * payloadlen : the size of the payload (bytes). Valid values are between 0 and
 *              268,435,455, although the upper limit isn't currently enforced.
 * payload :    pointer to the data to send. If payloadlen > 0 this must be a
 *              valid memory location.
 * qos :        integer value 0, 1 or 2 indicating the Quality of Service to be
 *              used for the will.
 * retain :     set to true to make the will a retained message.
 *
 * Returns 0 on success, 1 on failure.
 */


mosq_EXPORT int mosquitto_connect(struct mosquitto *mosq, const char *host, int port, int keepalive, bool clean_session);
/* Connect to an MQTT broker.
 * 
 * mosq :          a valid mosquitto instance
 * host :          the hostname or ip address of the broker to connect to
 * port :          the network port to connect to. Usually 1883.
 * keepalive :     the number of seconds after which the broker should send a
 *                 PING message to the client if no other messages have been
 *                 exchanged in that time.
 * clean_session : set to true to instruct the broker to clean all messages and
 *                 subscriptions on disconnect, false to instruct it to keep
 *                 them. See the man page mqtt(7) for more details.
 *
 * Returns 0 on success, 1 on failure.
 */

mosq_EXPORT int mosquitto_disconnect(struct mosquitto *mosq);
/* Disconnect from the broker.
 * 
 * mosq : a valid mosquitto instance
 *
 * Returns 0 on success, 1 on failure.
 */

mosq_EXPORT int mosquitto_publish(struct mosquitto *mosq, uint16_t *mid, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain);
/* Publish a message
 * 
 * mosq :       a valid mosquitto instance
 * mid :        pointer to a uint16_t. If not NULL, the function will set this
 *              to the message id of this particular message. This can be then
 *              used with the publish callback to determine when the message
 *              has been sent.
 *              Note that although the MQTT protocol doesn't use message ids
 *              for messages with QoS=0, libmosquitto assigns them message ids
 *              so they can be tracked with this parameter.
 * topic :      the topic to publish the message on
 * payloadlen : the size of the payload (bytes). Valid values are between 0 and
 *              268,435,455, although the upper limit isn't currently enforced.
 * payload :    pointer to the data to send. If payloadlen > 0 this must be a
 *              valid memory location.
 * qos :        integer value 0, 1 or 2 indicating the Quality of Service to be
 *              used for the message.
 * retain :     set to true to make the message retained.
 *
 * Returns 0 on success, 1 on failure.
 */

mosq_EXPORT int mosquitto_subscribe(struct mosquitto *mosq, uint16_t *mid, const char *sub, int qos);
/* Subscribe to a topic
 * 
 * mosq : a valid mosquitto instance
 * mid :        pointer to a uint16_t. If not NULL, the function will set this
 *              to the message id of this particular message. This can be then
 *              used with the subscribe callback to determine when the message
 *              has been sent.
 * sub :  the subscription pattern
 * qos :  the requested Quality of Service for this subscription
 *
 * Returns 0 on success, 1 on failure.
 */

mosq_EXPORT int mosquitto_unsubscribe(struct mosquitto *mosq, uint16_t *mid, const char *sub);
/* Unsubscribe from a topic
 * 
 * mosq : a valid mosquitto instance
 * mid :        pointer to a uint16_t. If not NULL, the function will set this
 *              to the message id of this particular message. This can be then
 *              used with the unsubscribe callback to determine when the message
 *              has been sent.
 * sub :  the unsubscription pattern
 *
 * Returns 0 on success, 1 on failure.
 */

mosq_EXPORT int mosquitto_message_copy(struct mosquitto_message *dst, const struct mosquitto_message *src);
/* Copy the contents of a mosquitto message to another message.
 * Useful for preserving a message received in the on_message() callback.
 *
 * Both dst and src must point to valid memory locations.
 *
 * Return 0 on success, 1 on failure.
 */

mosq_EXPORT void mosquitto_message_free(struct mosquitto_message **message);
/* Completely free a mosquitto message structure. */

mosq_EXPORT int mosquitto_loop(struct mosquitto *mosq, int timeout);
/* The main network loop for the client. You must call this frequently in order
 * to keep communications between the client and broker working.
 *
 * This calls select() to monitor the client network socket. If you want to
 * integrate mosquitto client operation with your own select() call, use
 * mosquitto_socket(), mosquitto_loop_read(), mosquitto_loop_write() and
 * mosquitto_loop_misc().
 *
 * mosq :    a valid mosquitto instance
 * timeout : Maximum number of milliseconds to wait for network activity in the
 *           select() call before timing out. Set to 0 for instant return. Set negative
 *           to use the default of 1000ms.
 *
 * Returns 0 on success, 1 on failure.
 */

mosq_EXPORT int mosquitto_socket(struct mosquitto *mosq);
/* Return the socket handle for a mosquitto instance. Useful if you want to
 * include a mosquitto client in your own select() calls.
 *
 * mosq : a valid mosquitto instance.
 *
 * Returns a socket handle on success, -1 on failure.
 */

mosq_EXPORT int mosquitto_loop_read(struct mosquitto *mosq);
/* Carry out network read operations.
 * This should only be used if you are not using mosquitto_loop() and are
 * monitoring the client network socket for activity yourself.
 *
 * mosq : a valid mosquitto instance
 *
 * Returns 0 on success, 1 on failure.
 */

mosq_EXPORT int mosquitto_loop_write(struct mosquitto *mosq);
/* Carry out network write operations.
 * This should only be used if you are not using mosquitto_loop() and are
 * monitoring the client network socket for activity yourself.
 *
 * mosq : a valid mosquitto instance
 *
 * Returns 0 on success, 1 on failure.
 */

mosq_EXPORT int mosquitto_loop_misc(struct mosquitto *mosq);
/* Carry out miscellaneous operations required as part of the network loop.
 * This should only be used if you are not using mosquitto_loop() and are
 * monitoring the client network socket for activity yourself.
 *
 * This function deals with handling PINGs and checking whether messages need
 * to be retried, so should be called fairly frequently.
 *
 * mosq : a valid mosquitto instance
 */



mosq_EXPORT void mosquitto_connect_callback_set(struct mosquitto *mosq, void (*on_connect)(void *, int));
/* Set the connect callback. This is called when the broker sends a CONNACK
 * message in response to a connection.
 * The callback function should be in the following form:
 * 
 * void callback(void *obj, int rc)
 *
 * obj : the user data provided to mosquitto_new().
 * rc :  the return code.
 *       0 : success
 *       1 : connection refused (unacceptable protocol version)
 *       2 : connection refused (identifier rejected)
 *       3 : connection refused (broker unavailable)
 *       4-255 : reserved for future use
 */
 
mosq_EXPORT void mosquitto_disconnect_callback_set(struct mosquitto *mosq, void (*on_disconnect)(void *));
/* Set the disconnect callback. This is called when the broker has received the
 * DISCONNECT command and has disconnected.
 *
 * The callback function should be in the following form:
 * 
 * void callback(void *obj)
 *
 * obj : the user data provided to mosquitto_new().
 */
 
mosq_EXPORT void mosquitto_publish_callback_set(struct mosquitto *mosq, void (*on_publish)(void *, uint16_t));
/* Set the publish callback. This is called when a message initiated with
 * mosquitto_publish() has been sent to the broker successfully.
 * The callback function should be in the following form:
 * 
 * void callback(void *obj, uint16_t mid)
 *
 * obj : the user data provided to mosquitto_new().
 * mid : the message id of the sent message.
 */

mosq_EXPORT void mosquitto_message_callback_set(struct mosquitto *mosq, void (*on_message)(void *, const struct mosquitto_message *));
/* Set the message callback. This is called when a message is received from the
 * broker.
 * The callback function should be in the following form:
 * 
 * void callback(void *obj, const struct mosquitto_message *message)
 *
 * obj :     the user data provided to mosquitto_new().
 * message : the message data - see above for struct details.
 *
 * The message variable and associated memory will be free'd by the library
 * after the callback has run. The client should make copies of any of the data
 * it requires. 
 */

mosq_EXPORT void mosquitto_subscribe_callback_set(struct mosquitto *mosq, void (*on_subscribe)(void *, uint16_t, int, const uint8_t *));
/* Set the subscribe callback. This is called when the broker responds to a
 * subscription request.
 * The callback function should be in the following form:
 * 
 * void callback(void *obj, uint16_t mid, int qos_count, const uint8_t *granted_qos)
 *
 * obj :         the user data provided to mosquitto_new().
 * mid :         the message id of the subscribe message.
 * qos_count :   the number of granted subscriptions (size of granted_qos)
 * granted_qos : an array of integers indicating the granted QoS for each of
 *               the subscriptions.
 */

mosq_EXPORT void mosquitto_unsubscribe_callback_set(struct mosquitto *mosq, void (*on_unsubscribe)(void *, uint16_t));
/* Set the unsubscribe callback. This is called when the broker responds to an
 * unsubscription request.
 * The callback function should be in the following form:
 * 
 * void callback(void *obj, uint16_t mid)
 *
 * obj : the user data provided to mosquitto_new().
 * mid : the message id of the unsubscribe message.
 */


mosq_EXPORT void mosquitto_message_retry_set(struct mosquitto *mosq, unsigned int message_retry);
/* Set the number of seconds to wait before retrying messages. This applies to
 * publish messages with QoS>0. May be called at any time.
 *
 * mosq :          a valid mosquitto instance
 * message_retry : the number of seconds to wait for a response before
 *                 retrying. Defaults to 60.
 */

#ifdef __cplusplus
}
#endif

#endif
