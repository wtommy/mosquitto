CC=cc
CFLAGS=-I../library -Wall -ggdb
LDFLAGS=-lsqlite3

.PHONY: all clean

all : state

state : state.o database.o managed_send.o messages.o net.o raw_send.o read_handle.o read_handle_client.o util.o
	${CC} $(LDFLAGS) $^ -o $@

state.o : state.c ../library/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

database.o : ../server/database.c ../library/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

managed_send.o : ../library/managed_send.c ../library/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

messages.o : ../library/messages.c ../library/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

net.o : ../library/net.c ../library/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

raw_send.o : ../library/raw_send.c ../library/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

read_handle.o : ../library/read_handle.c ../library/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

read_handle_client.o : ../library/read_handle_client.c ../library/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

util.o : ../library/util.c ../library/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

clean : 
	-rm -f *.o state
