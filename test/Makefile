include ../config.mk

CC=cc
CFLAGS=-I../src -I. -I.. -Wall -ggdb -DDEBUG -DWITH_CLIENT
LDFLAGS=-lsqlite3 -nopie
OBJS=context.o database.o logging.o memory.o net.o raw_send.o raw_send_client.o read_handle.o read_handle_client.o util.o

.PHONY: all clean

all : random_client

random_client : random_client.o ${OBJS}
	${CC} $^ -o $@ ${LDFLAGS}

random_client.o : random_client.c ../src/mqtt3.h
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

read_handle.o : ../src/read_handle.c ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

read_handle_client.o : ../src/read_handle_client.c ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

util.o : ../src/util.c ../src/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

clean : 
	-rm -f *.o random_client
