CC=cc
CFLAGS=-I../lib -Wall -ggdb
LDFLAGS=-L../lib ../lib/libmosquitto.so.0
INSTALL=install
prefix=/usr/local

.PHONY: all install uninstall clean

all : mosquitto_pub mosquitto_sub

mosquitto_pub : pub_client.o
	${CC} $^ -o $@ ${LDFLAGS}

mosquitto_sub : sub_client.o
	${CC} $^ -o $@ ${LDFLAGS}

install : all
	$(INSTALL) -d ${DESTDIR}$(prefix)/bin
	$(INSTALL) -s mosquitto_pub ${DESTDIR}${prefix}/bin/mosquitto_pub
	$(INSTALL) -s mosquitto_sub ${DESTDIR}${prefix}/bin/mosquitto_sub

uninstall :
	-rm -f ${DESTDIR}${prefix}/bin/mosquitto_pub
	-rm -f ${DESTDIR}${prefix}/bin/mosquitto_sub

clean : 
	-rm -f *.o mosquitto_pub mosquitto_sub
