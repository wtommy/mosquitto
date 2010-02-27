CC=cc
CFLAGS=-I../src -I. -Wall -ggdb -DWITH_CLIENT
LDFLAGS=-lsqlite3 -nopie
OBJS=client_shared.o context.o database.o logging.o memory.o net.o raw_send.o raw_send_client.o read_handle.o read_handle_client.o util.o
INSTALL=install
prefix=/usr/local

.PHONY: all install uninstall clean

all : mosquitto_pub mosquitto_sub

mosquitto_pub : pub_client.o $(OBJS)
	${CC} $^ -o $@ ${LDFLAGS}

mosquitto_sub : sub_client.o $(OBJS)
	${CC} $^ -o $@ ${LDFLAGS}

pub_client.o : pub_client.c client_shared.h ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

sub_client.o : sub_client.c client_shared.h ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

client_shared.o : client_shared.c client_shared.h ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

context.o : ../src/context.c ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

database.o : ../src/database.c ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

logging.o : ../src/logging.c ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

memory.o : ../src/memory.c ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

net.o : ../src/net.c ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

raw_send.o : ../src/raw_send.c ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

raw_send_client.o : ../src/raw_send_client.c ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

raw_send_server.o : ../src/raw_send_server.c ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

read_handle.o : ../src/read_handle.c ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

read_handle_client.o : ../src/read_handle_client.c ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

read_handle_server.o : ../src/read_handle_server.c ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

util.o : ../src/util.c ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

install : all
	$(INSTALL) -d ${DESTDIR}$(prefix)/bin
	$(INSTALL) -s mosquitto_pub ${DESTDIR}${prefix}/bin/mosquitto_pub
	$(INSTALL) -s mosquitto_sub ${DESTDIR}${prefix}/bin/mosquitto_sub

uninstall :
	-rm -f ${DESTDIR}${prefix}/bin/mosquitto_pub
	-rm -f ${DESTDIR}${prefix}/bin/mosquitto_sub

clean : 
	-rm -f *.o mosquitto_pub mosquitto_sub
