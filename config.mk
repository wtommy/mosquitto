VERSION=0.8pre
TIMESTAMP:=$(shell date "+%F %T%z")

#MANCOUNTRIES=en_GB

CFLAGS=-I. -I.. -ggdb -Wall -O2
LDFLAGS=-lsqlite3
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
