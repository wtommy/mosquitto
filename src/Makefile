include ../config.mk

CFLAGS_FINAL=${CFLAGS} -DVERSION="\"${VERSION}\"" -DTIMESTAMP="\"${TIMESTAMP}\"" -DWITH_BROKER

.PHONY: all install uninstall clean reallyclean

all : mosquitto

mosquitto : mosquitto.o bridge.o conf.o context.o database.o logging.o loop.o memory_mosq.o persist.o net.o net_mosq.o raw_send.o raw_send_server.o read_handle.o read_handle_client.o read_handle_server.o security.o security_external.o send_base_mosq.o send_client_mosq.o subs.o util.o util_mosq.o will_mosq.o
	${CC} $^ -o $@ ${LDFLAGS} ${LIBS}

mosquitto.o : mosquitto.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

bridge.o : bridge.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@
	
conf.o : conf.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@
	
context.o : context.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

database.o : database.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

logging.o : logging.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

loop.o : loop.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

memory_mosq.o : ../lib/memory_mosq.c ../lib/memory_mosq.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

net.o : net.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

net_mosq.o : ../lib/net_mosq.c ../lib/net_mosq.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@
	
persist.o : persist.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@
	
raw_send.o : raw_send.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

raw_send_server.o : raw_send_server.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

read_handle.o : read_handle.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

read_handle_client.o : read_handle_client.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

read_handle_server.o : read_handle_server.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

security.o : security.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

security_external.o : security_external.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

send_base_mosq.o : ../lib/send_base_mosq.c ../lib/send_mosq.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

send_client_mosq.o : ../lib/send_client_mosq.c ../lib/send_mosq.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

subs.o : subs.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

util.o : util.c mqtt3.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

util_mosq.o : ../lib/util_mosq.c ../lib/util_mosq.h
	${CC} $(CFLAGS_FINAL) -c $< -o $@

will_mosq.o : ../lib/will_mosq.c ../lib/will_mosq.h
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
