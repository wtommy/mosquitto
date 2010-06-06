include ../../config.mk

.PHONY : clean

libmosquittopp.so : mosquittopp.o
	$(CXX) -shared -Wl,-soname,libmosquittopp.so.0 $< -o $@

mosquittopp.o : mosquittopp.cpp mosquittopp.h
	$(CXX) $(CFLAGS) -fPIC -c $< -o $@

clean :
	-rm -f *.o libmosquittopp.so
