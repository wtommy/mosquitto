include ../../config.mk

.PHONY : all clean install

all : mosquitto.pyc

install : all
	python ./setup.py install --prefix=${prefix} --root=${DESTDIR}

mosquitto.pyc : mosquitto.py
	python ./setup.py build

clean :
	-rm -rf build mosquitto.pyc
