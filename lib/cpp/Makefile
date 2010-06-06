include ../../config.mk

.PHONY : clean install

all : libmosquittopp.so.0

install : all
	
clean :
	-rm -f *.o libmosquittopp.so.0

libmosquittopp.so.0 : mosquittopp.o
	$(CXX) -shared -Wl,-soname,libmosquittopp.so.0 $< -o $@ -L.. -lmosquitto

mosquittopp.o : mosquittopp.cpp mosquittopp.h
	$(CXX) $(CFLAGS) -fPIC -c $< -o $@

