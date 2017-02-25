
#
# Makefile.in for smppbox plugin API example.
#
# This file is currently maintained manually. There is currently
# no generation out of automake. This will change as soon as we
# switch to our new autoconf/configure build process.
#

SHELL = /bin/bash

srcdir = .
top_srcdir = ..

prefix = /opt/kannel
exec_prefix = ${prefix}

bindir = ${exec_prefix}/bin
sbindir = ${exec_prefix}/sbin
libexecdir = ${exec_prefix}/libexec
datadir = ${datarootdir}
datarootdir = ${prefix}/share
sysconfdir = ${prefix}/etc
sharedstatedir = ${prefix}/com
localstatedir = ${prefix}/var
logdir = ${prefix}/logs
libdir = ${exec_prefix}/lib
infodir = ${datarootdir}/info
mandir = ${datarootdir}/man
includedir = ${prefix}/include
oldincludedir = /usr/include
docdir = ${datarootdir}/doc/${PACKAGE}

DESTDIR =

top_builddir = .

ACLOCAL = @ACLOCAL@
AUTOCONF = @AUTOCONF@
AUTOHEADER = @AUTOHEADER@

INSTALL = /usr/bin/install -c
INSTALL_PROGRAM = ${INSTALL} $(AM_INSTALL_PROGRAM_FLAGS)
INSTALL_DATA = ${INSTALL} -m 644
INSTALL_SCRIPT = ${INSTALL}
transform = s,x,x,

NORMAL_INSTALL = :
PRE_INSTALL = :
POST_INSTALL = :
NORMAL_UNINSTALL = :
PRE_UNINSTALL = :
POST_UNINSTALL = :
CC = gcc
MAKEINFO = @MAKEINFO@
PACKAGE = smppbox
RANLIB = ranlib
SHELL = /bin/bash
VERSION = 1.8.5-meta-r1205
SUFFIX = -1.8.5-meta-r1205-cvs-20090529
LEX = @LEX@
PERL = @PERL@
YACC = @YACC@

CFLAGS = `../pb-config --cflags` -I..
LDFLAGS = `../pb-config --libs` -L..

# platform specific shared library extentions and flags
DSO_EXT = so
DSO_CFLAGS = $(CFLAGS) -fPIC
#DSO_LDFLAGS = /usr/local/lib/kannel/libgwlib.a /usr/local/lib/kannel/libgw.a -shared
DSO_LDFLAGS = `../pb-config --libs` -lgw -lgwlib -ldb -L../gwlib -shared

srcs = $(wildcard *.c)
objs = $(srcs:.c=.lo)
dsos = $(objs:.lo=.so)

.SUFFIXES: $(SUFFIXES) .c .lo .so

.c.lo:
	$(CC) $(DSO_CFLAGS) -o $@ -c $<
	
.lo.so:
	$(CC) -o $@ $< $(DSO_LDFLAGS)

.lo.dylib:
	$(CC) -o $@ $< $(DSO_LDFLAGS) $(LDFLAGS)
	
.lo.dll:
	$(CC) -o $@ $< $(DSO_LDFLAGS) $(LDFLAGS)
	
all: $(dsos)

clean:
	find . -name "*.so" | xargs rm -f

install:
	
