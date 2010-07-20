include ../config.mk

.PHONY : clean install

all : libmosquitto.so.0
	make -C cpp

install : all
	make -C cpp install

clean :
	-rm -f *.o libmosquitto.so.0 libmosquitto.so
	make -C cpp clean

libmosquitto.so.0 : mosquitto.o
	$(CC) -shared -Wl,-soname,libmosquitto.so.0 $< -o $@
	ln -sf libmosquitto.so.0 libmosquitto.so

mosquitto.o : mosquitto.c mosquitto.h
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

