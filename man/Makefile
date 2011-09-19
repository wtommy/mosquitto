include ../config.mk

.PHONY : all clean install uninstall dist 

DB2M:=$(shell which docbook2man.pl 2> /dev/null)
ifeq ($(strip $(DB2M)),)
  DB2XM:=$(shell which docbook2x-man 2> /dev/null)
  ifeq ($(strip $(DB2XM)),)
    DOCBOOK2MAN=docbook2man
  else
    DOCBOOK2MAN=docbook2x-man
  endif
else
  DOCBOOK2MAN=docbook2man.pl
endif

all :  mosquitto.8 mosquitto.conf.5 mosquitto_pub.1 mosquitto_sub.1 mqtt.7 libmosquitto.3

clean :

reallyclean : clean
	-rm -f *.orig

dist : mosquitto.8 mosquitto.conf.5 mosquitto_pub.1 mosquitto_sub.1 mqtt.7 libmosquitto.3

install :
	$(INSTALL) -d ${DESTDIR}$(mandir)/man8
	$(INSTALL) -m 644 mosquitto.8 ${DESTDIR}${mandir}/man8/mosquitto.8
	$(INSTALL) -d ${DESTDIR}$(mandir)/man5
	$(INSTALL) -m 644 mosquitto.conf.5 ${DESTDIR}${mandir}/man5/mosquitto.conf.5
	$(INSTALL) -d ${DESTDIR}$(mandir)/man1
	$(INSTALL) -m 644 mosquitto_pub.1 ${DESTDIR}${mandir}/man1/mosquitto_pub.1
	$(INSTALL) -m 644 mosquitto_sub.1 ${DESTDIR}${mandir}/man1/mosquitto_sub.1
	$(INSTALL) -d ${DESTDIR}$(mandir)/man7
	$(INSTALL) -m 644 mqtt.7 ${DESTDIR}${mandir}/man7/mqtt.7
	$(INSTALL) -d ${DESTDIR}$(mandir)/man3
	$(INSTALL) -m 644 libmosquitto.3 ${DESTDIR}${mandir}/man3/libmosquitto.3

uninstall :
	-rm -f ${DESTDIR}${mandir}/man8/mosquitto.8
	-rm -f ${DESTDIR}${mandir}/man5/mosquitto.conf.5
	-rm -f ${DESTDIR}${mandir}/man1/mosquitto_pub.1
	-rm -f ${DESTDIR}${mandir}/man1/mosquitto_sub.1
	-rm -f ${DESTDIR}${mandir}/man7/mqtt.7
	-rm -f ${DESTDIR}${mandir}/man3/libmosquitto.3

mosquitto.8 : mosquitto.8.xml
	$(DOCBOOK2MAN) mosquitto.8.xml

mosquitto.conf.5 : mosquitto.conf.5.xml
	$(DOCBOOK2MAN) mosquitto.conf.5.xml

mosquitto_pub.1 : mosquitto_pub.1.xml
	$(DOCBOOK2MAN) $^

mosquitto_sub.1 : mosquitto_sub.1.xml
	$(DOCBOOK2MAN) $^

mqtt.7 : mqtt.7.xml
	$(DOCBOOK2MAN) $^

libmosquitto.3 : libmosquitto.3.xml
	$(DOCBOOK2MAN) $^

potgen :
	 xml2po -o po/mosquitto/mosquitto.8.pot mosquitto.8.xml
	 xml2po -o po/mosquitto.conf/mosquitto.conf.5.pot mosquitto.conf.5.xml
	 xml2po -o po/mosquitto_pub/mosquitto_pub.1.pot mosquitto_pub.1.xml
	 xml2po -o po/mosquitto_sub/mosquitto_sub.1.pot mosquitto_sub.1.xml
	 xml2po -o po/mqtt/mqtt.7.pot mqtt.7.xml
	 xml2po -o po/libmosquitto/libmosquitto.3.pot libmosquitto.3.xml

# To merge new translations do:
# /usr/bin/xml2po -p de.po chapter1.xml > chapter1.de.xml
