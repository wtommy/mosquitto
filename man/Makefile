include ../config.mk

.PHONY : all clean install uninstall dist distclean

DOCBOOK2MAN=docbook2man.pl

all :  mosquitto.8 mosquitto.conf.5

clean :

reallyclean : clean
	-rm -f *.orig

dist : mosquitto.8 mosquitto.conf.5

install :
	$(INSTALL) -d ${DESTDIR}$(mandir)/man8
	$(INSTALL) -m 644 mosquitto.8 ${DESTDIR}${mandir}/man8/mosquitto.8
	$(INSTALL) -d ${DESTDIR}$(mandir)/man5
	$(INSTALL) -m 644 mosquitto.8 ${DESTDIR}${mandir}/man5/mosquitto.conf.5

uninstall :
	-rm -f ${DESTDIR}${mandir}/man8/mosquitto.8
	-rm -f ${DESTDIR}${mandir}/man5/mosquitto.conf.5

mosquitto.8 : mosquitto.8.xml
	$(DOCBOOK2MAN) mosquitto.8.xml

mosquitto.conf.5 : mosquitto.conf.5.xml
	$(DOCBOOK2MAN) mosquitto.conf.5.xml

distclean :
	-rm -f mosquitto*.8
	-rm -f mosquitto.conf*.5

# To merge new translations do:
# /usr/bin/xml2po -p de.po chapter1.xml > chapter1.de.xml
