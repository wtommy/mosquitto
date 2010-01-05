include config.mk

DIRS=man src
DISTDIRS=man

.PHONY : all mosquitto clean reallyclean install uninstall dist distclean sign copy

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
	cp -r logo man src windows ChangeLog.txt COPYING Makefile compiling.txt config.h config.mk readme.txt mosquitto.conf dist/mosquitto-${VERSION}/
	cd dist; tar -zcf mosquitto-${VERSION}.tar.gz mosquitto-${VERSION}/
	man2html man/mosquitto.8 > dist/mosquitto-8.html
	man2html man/mosquitto.conf.5 > dist/mosquitto-conf-5.html
	sed -i 's#http://localhost/cgi-bin/man/man2html?5+mosquitto.conf#mosquitto-conf-5.html#' dist/mosquitto-8.html dist/mosquitto-conf-5.html
	sed -i 's#http://localhost/cgi-bin/man/man2html?8+mosquitto#mosquitto-8.html#' dist/mosquitto-8.html dist/mosquitto-conf-5.html
	sed -i 's#http://localhost/cgi-bin/man/man2html#http://mosquitto.atchoo.org/#' dist/mosquitto-8.html dist/mosquitto-conf-5.html

distclean : clean
	@for d in ${DISTDIRS}; do $(MAKE) -C $${d} distclean; done
	
	-rm -rf dist/

sign : dist
	cd dist; gpg --detach-sign -a mosquitto-${VERSION}.tar.gz

copy : sign
	cd dist; scp mosquitto-${VERSION}.tar.gz mosquitto-${VERSION}.tar.gz.asc atchoo:mosquitto.atchoo.org/files/source/
	cd dist; scp *.html atchoo:mosquitto.atchoo.org/man/
	scp ChangeLog.txt atchoo:mosquitto.atchoo.org/

