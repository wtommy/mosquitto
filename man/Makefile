include ../config.mk

.PHONY : all clean install uninstall dist distclean

DOCBOOK2MAN=docbook2man.pl

all :  mosquitto.8 mosquitto.conf.5 mosquitto_pub.1 mosquitto_sub.1 mqtt.7

clean :

reallyclean : clean
	-rm -f *.orig

dist : mosquitto.8 mosquitto.conf.5 mosquitto_pub.1 mosquitto_sub.1 mqtt.7

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

uninstall :
	-rm -f ${DESTDIR}${mandir}/man8/mosquitto.8
	-rm -f ${DESTDIR}${mandir}/man5/mosquitto.conf.5
	-rm -f ${DESTDIR}${mandir}/man1/mosquitto_pub.1
	-rm -f ${DESTDIR}${mandir}/man1/mosquitto_sub.1
	-rm -f ${DESTDIR}${mandir}/man1/mqtt.7

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

distclean :
	-rm -f mosquitto*.8
	-rm -f mosquitto.conf*.5
	-rm -f mosquitto_pub*.1
	-rm -f mosquitto_sub*.1

# To merge new translations do:
# /usr/bin/xml2po -p de.po chapter1.xml > chapter1.de.xml
