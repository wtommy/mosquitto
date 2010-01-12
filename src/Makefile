include ../config.mk

CFLAGS_FINAL=${CFLAGS} -DVERSION="\"${VERSION}\"" -DTIMESTAMP="\"${TIMESTAMP}\"" -DWITH_BROKER

.PHONY: all install uninstall clean reallyclean

all : mosquitto

mosquitto : mosquitto.o conf.o context.o database.o logging.o memory.o net.o raw_send.o raw_send_server.o read_handle.o read_handle_server.o
	${CC} $^ -o $@ ${LDFLAGS}

mosquitto.o : mosquitto.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

conf.o : conf.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@
	
context.o : context.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

database.o : database.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

logging.o : logging.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

memory.o : memory.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

net.o : net.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

raw_send.o : raw_send.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

raw_send_server.o : raw_send_server.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

read_handle.o : read_handle.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

read_handle_server.o : read_handle_server.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

util.o : util.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

install :
	$(INSTALL) -d ${DESTDIR}$(prefix)/sbin
	$(INSTALL) -s mosquitto ${DESTDIR}${prefix}/sbin/mosquitto

uninstall :
	-rm -f ${DESTDIR}${prefix}/sbin/mosquitto

clean : 
	-rm -f *.o mosquitto

reallyclean : clean
	-rm -rf *.orig *.db
