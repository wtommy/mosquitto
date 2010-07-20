#ifndef _READ_HANDLE_H_
#define _READ_HANDLE_H_

int _mosquitto_packet_handle(struct mosquitto *mosq);
int _mosquitto_handle_connack(struct mosquitto *mosq);
int _mosquitto_handle_pingreq(struct mosquitto *mosq);
int _mosquitto_handle_pingresp(struct mosquitto *mosq);
int _mosquitto_handle_puback(struct mosquitto *mosq);
int _mosquitto_handle_publish(struct mosquitto *mosq);
int _mosquitto_handle_suback(struct mosquitto *mosq);
int _mosquitto_handle_unsuback(struct mosquitto *mosq);


#endif
