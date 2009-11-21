.PHONY : all clean install uninstall dist distclean

DOCBOOK2MAN=docbook2man.pl

all :  mosquitto.8

clean :
	-rm mosquitto*.8

install :
	$(INSTALL) -d ${DESTDIR}$(mandir)/man8
	$(INSTALL) -m 644 mosquitto.8 ${DESTDIR}${mandir}/man8/mosquitto.8

uninstall :
	-rm -f ${DESTDIR}${mandir}/man8/mosquitto.8

mosquitto.8 : mosquitto.8.xml
	$(DOCBOOK2MAN) mosquitto.8.xml

distclean :
	-rm -f mosquitto.8

# To merge new translations do:
# /usr/bin/xml2po -p de.po chapter1.xml > chapter1.de.xml
