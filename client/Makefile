CC=cc
CFLAGS=-I../library -Wall

.PHONY: all clean

all : client

client : client.c ../library/mqtt3.h
	${CC} $(CFLAGS) $< -o $@

clean : 
	-rm -f client
