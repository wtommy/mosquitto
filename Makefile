include config.mk

DIRS=client man src
DISTDIRS=man

.PHONY : all mosquitto clean reallyclean install uninstall dist sign copy

all : mosquitto

mosquitto :
	for d in ${DIRS}; do $(MAKE) -C $${d}; done

clean :
	for d in ${DIRS}; do $(MAKE) -C $${d} clean; done

reallyclean : 
	for d in ${DIRS}; do $(MAKE) -C $${d} reallyclean; done
	-rm -f *.orig

install : mosquitto
	@for d in ${DIRS}; do $(MAKE) -C $${d} install; done
	$(INSTALL) -d ${DESTDIR}/etc
	$(INSTALL) -m 644 mosquitto.conf ${DESTDIR}/etc/mosquitto.conf

uninstall :
	@for d in ${DIRS}; do $(MAKE) -C $${d} uninstall; done

dist : reallyclean
	@for d in ${DISTDIRS}; do $(MAKE) -C $${d} dist; done
	
	mkdir -p dist/mosquitto-${VERSION}
	cp -r client logo man misc security service src windows ChangeLog.txt COPYING Makefile compiling.txt config.h config.mk readme.txt mosquitto.conf dist/mosquitto-${VERSION}/
	cd dist; tar -zcf mosquitto-${VERSION}.tar.gz mosquitto-${VERSION}/
	for m in mosquitto.8 mosquitto.conf.5 mosquitto_pub.1 mosquitto_sub.1 mqtt.7; \
		do \
		hfile=$$(echo $${m} | sed -e 's/\./-/g'); \
		man2html man/$${m} > dist/$${hfile}.html; \
		sed -i 's#http://localhost/cgi-bin/man/man2html?8+mosquitto#mosquitto-8.html#' dist/$${hfile}.html; \
		sed -i 's#http://localhost/cgi-bin/man/man2html?5+mosquitto.conf#mosquitto-conf-5.html#' dist/$${hfile}.html; \
		sed -i 's#http://localhost/cgi-bin/man/man2html?1+mosquitto_pub#mosquitto_pub-1.html#' dist/$${hfile}.html; \
		sed -i 's#http://localhost/cgi-bin/man/man2html?1+mosquitto_sub#mosquitto_sub-1.html#' dist/$${hfile}.html; \
		sed -i 's#http://localhost/cgi-bin/man/man2html?7+mmqtt#mqtt-7.html#' dist/$${hfile}.html; \
		sed -i 's#http://localhost/cgi-bin/man/man2html#http://mosquitto.atchoo.org/#' dist/$${hfile}.html; \
	done


sign : dist
	cd dist; gpg --detach-sign -a mosquitto-${VERSION}.tar.gz

copy : sign
	cd dist; scp mosquitto-${VERSION}.tar.gz mosquitto-${VERSION}.tar.gz.asc atchoo:mosquitto.atchoo.org/files/source/
	cd dist; scp *.html atchoo:mosquitto.atchoo.org/man/
	scp ChangeLog.txt atchoo:mosquitto.atchoo.org/

