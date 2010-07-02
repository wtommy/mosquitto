include ../config.mk

.PHONY : clean install

all : libmosquitto.so.0
	make -C cpp

install : all
	make -C cpp install

clean :
	-rm -f *.o libmosquitto.so.0 libmosquitto.so
	make -C cpp clean

libmosquitto.so.0 : mosquitto.o database_mosq.o net_mosq.o
	$(CC) -shared -Wl,-soname,libmosquitto.so.0 $^ -o $@ -lsqlite3
	ln -sf libmosquitto.so.0 libmosquitto.so

mosquitto.o : mosquitto.c mosquitto.h
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

database_mosq.o : database_mosq.c database_mosq.h
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

net_mosq.o : net_mosq.c net_mosq.h
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

