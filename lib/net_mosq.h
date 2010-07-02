#ifndef _NET_MOSQ_H_
#define _NET_MOSQ_H_

#include <stdint.h>

int _mosquitto_socket_connect(const char *host, uint16_t port);

#endif
