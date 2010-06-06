include ../config.mk

.PHONY : clean install

all : libmosquitto.so
	make -C cpp

install : all
	make -C cpp install

clean :
	-rm -f *.o libmosquitto.so
	make -C cpp clean

libmosquitto.so : mosquitto.o
	$(CXX) -shared -Wl,-soname,libmosquitto.so.0 $< -o $@

mosquitto.o : mosquitto.c mosquitto.h
	$(CXX) $(CFLAGS) -fPIC -c $< -o $@

