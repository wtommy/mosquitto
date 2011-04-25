# Also bump lib/mosquitto.h, lib/python/setup.py, CMakeLists.txt
VERSION=0.9.90
TIMESTAMP:=$(shell date "+%F %T%z")

#MANCOUNTRIES=en_GB

CFLAGS=-I. -I.. -ggdb -Wall -O3 -I../lib
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
