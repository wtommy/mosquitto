CFLAGS=-Wall -ggdb
LDFLAGS=-lmosquittopp

.PHONY: all clean

all : mqtt_temperature_conversion

mqtt_temperature_conversion : main.o temperature_conversion.o
	${CC} $^ -o $@ ${LDFLAGS}

clean : 
	-rm -f *.o mqtt_temperature_conversion
