# Also bump lib/mosquitto.h, lib/python/setup.py, CMakeLists.txt
VERSION=0.14
TIMESTAMP:=$(shell date "+%F %T%z")

#MANCOUNTRIES=en_GB

CFLAGS=-I. -I.. -ggdb -Wall -O2 -I../lib

UNAME:=$(shell uname -s)
ifeq ($(UNAME),QNX)
	LIBS=-lsocket
else
	LIBS=
endif

LDFLAGS=
# Add -lwrap to LDFLAGS if compiling with tcp wrappers support.

CC=gcc
INSTALL=install
XGETTEXT=xgettext
MSGMERGE=msgmerge
MSGFMT=msgfmt
DOCBOOK2MAN=docbook2man.pl

prefix=/usr/local
mandir=${prefix}/share/man
localedir=${prefix}/share/locale
