include config.mk

DIRS=lib client src man
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
	$(INSTALL) -d ${DESTDIR}/etc/mosquitto
	$(INSTALL) -m 644 mosquitto.conf ${DESTDIR}/etc/mosquitto/mosquitto.conf

uninstall :
	@for d in ${DIRS}; do $(MAKE) -C $${d} uninstall; done

dist : reallyclean
	@for d in ${DISTDIRS}; do $(MAKE) -C $${d} dist; done
	
	mkdir -p dist/mosquitto-${VERSION}
	cp -r client compat lib logo man misc security service src ChangeLog.txt CMakeLists.txt COPYING Makefile compiling.txt config.h config.mk readme.txt mosquitto.conf aclfile.example pwfile.example dist/mosquitto-${VERSION}/
	cd dist; tar -zcf mosquitto-${VERSION}.tar.gz mosquitto-${VERSION}/
	for m in libmosquitto.3 mosquitto.8 mosquitto.conf.5 mosquitto_pub.1 mosquitto_sub.1 mqtt.7; \
		do \
		hfile=$$(echo $${m} | sed -e 's/\./-/g'); \
		man2html man/$${m} > dist/$${hfile}.html; \
		sed -i 's#http://localhost/cgi-bin/man/man2html?8+mosquitto#mosquitto-8.html#' dist/$${hfile}.html; \
		sed -i 's#http://localhost/cgi-bin/man/man2html?5+mosquitto.conf#mosquitto-conf-5.html#' dist/$${hfile}.html; \
		sed -i 's#http://localhost/cgi-bin/man/man2html?1+mosquitto_pub#mosquitto_pub-1.html#' dist/$${hfile}.html; \
		sed -i 's#http://localhost/cgi-bin/man/man2html?1+mosquitto_sub#mosquitto_sub-1.html#' dist/$${hfile}.html; \
		sed -i 's#http://localhost/cgi-bin/man/man2html?7+mqtt#mqtt-7.html#' dist/$${hfile}.html; \
		sed -i 's#http://localhost/cgi-bin/man/man2html?5+hosts_access#http://www.linuxmanpages.com/man5/hosts_access.5.php#' dist/$${hfile}.html; \
		sed -i 's#http://localhost/cgi-bin/man/man2html#http://mosquitto.org/#' dist/$${hfile}.html; \
		sed -i '1,2d' dist/$${hfile}.html; \
	done


sign : dist
	cd dist; gpg --detach-sign -a mosquitto-${VERSION}.tar.gz

copy : sign
	cd dist; scp mosquitto-${VERSION}.tar.gz mosquitto-${VERSION}.tar.gz.asc mosquitto:mosquitto.org/files/source/
	cd dist; scp *.html mosquitto:mosquitto.org/man/
	scp ChangeLog.txt mosquitto:mosquitto.org/

