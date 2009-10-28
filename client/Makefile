CC=cc
CFLAGS=-I../library -Wall -ggdb
LDFLAGS=

.PHONY: all clean

all : state

state : state.o net.o raw_send.o util.o
	${CC} $(LDFLAGS) $^ -o $@

state.o : state.c ../library/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

net.o : ../library/net.c ../library/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

raw_send.o : ../library/raw_send.c ../library/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

util.o : ../library/util.c ../library/mqtt3.h
	${CC} $(CFLAGS) -c $< -o $@

clean : 
	-rm -f *.o state
