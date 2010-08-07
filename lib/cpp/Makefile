include ../../config.mk

.PHONY : clean install

all : libmosquittopp.so.0

install : all
	$(INSTALL) -d ${DESTDIR}$(prefix)/lib/
	$(INSTALL) -s libmosquittopp.so.0 ${DESTDIR}${prefix}/lib/libmosquittopp.so.0
	ln -sf libmosquittopp.so.0 ${DESTDIR}${prefix}/lib/libmosquittopp.so
	$(INSTALL) -d ${DESTDIR}${prefix}/include/
	$(INSTALL) mosquittopp.h ${DESTDIR}${prefix}/include/mosquittopp.h
	
clean :
	-rm -f *.o libmosquittopp.so.0

libmosquittopp.so.0 : mosquittopp.o
	$(CXX) -shared -Wl,-soname,libmosquittopp.so.0 $< -o $@ ../libmosquitto.so.0

mosquittopp.o : mosquittopp.cpp mosquittopp.h
	$(CXX) $(CFLAGS) -fPIC -c $< -o $@

