CC=cc
CFLAGS=-I../library -Wall
LDFLAGS=

.PHONY: all clean

all : client

client : client.o raw_send.o util.o
	${CC} $(LDFLAGS) $^ -o $@

client.o : client.c ../library/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

raw_send.o : ../library/raw_send.c ../library/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

util.o : ../library/util.c ../library/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

clean : 
	-rm -f *.o client
