#
# Makefile for WapIT (WAP/SMS) Gateway.
#

#
# Define where the programs will be installed.
#

prefix = /usr/local
bindir = $(prefix)/bin

#
# Define here how the programs will be compiled. You can use or not use
# the pthread thread library, according to your wishes.
#
#

VERSION=$(shell head -1 VERSION)

# Name of PID file (uncomment and modify if you want to change the default)
#PID_FILE=-DPID_FILE=\"/your/value/here/gateway.pid\"

CC=gcc
LIBS=
CFLAGS=-Wall -g -DHAVE_THREADS=1 $(PID_FILE) -DVERSION=\"$(VERSION)\" -Igw -Igwlib
LDFLAGS=

MKDEPEND=$(CC) $(CFLAGS) -MM

# Some systems require ranlib to be run on a library after it is created.
# Some don't even have ranlib. Uncomment appropriately.
RANLIB=:
#RANLIB=ranlib

# For Linux, uncomment the following:
CFLAGS += -DHAVE_SOCKLEN_T
THREADLIB = -lpthread

# For FreeBSD, uncomment the following:
#LIBS += -lc_r
#THREADLIB = 

# Uncomment one of these if you want to use a malloc debugger.
#EFENCELIB = -lefence

# Generic libraries.
LIBS += $(THREADLIB) $(EFENCELIB) -lm

# For Solaris uncomment the following
#LIBS += -lsocket -lnsl

#
# You probably don't need to touch anything below this, if you're just
# compiling and installing the software.
#

progsrcs = \
	gw/bearerbox.c \
	gw/smsbox.c \
	gw/wapbox.c

progobjs = $(progsrcs:.c=.o)
progs = $(progsrcs:.c=)

gwsrcs = $(shell echo gw/*.c)
gwobjs = $(gwsrcs:.c=.o)

libsrcs = $(shell echo gwlib/*.c)
libobjs = $(libsrcs:.c=.o)

testsrcs = $(shell echo test/*.c)
testobjs = $(testsrcs:.c=.o)
testprogs = $(testsrcs:.c=)

srcs = $(shell echo */*.c)
objs = $(srcs:.c=.o)

all: progs tests

progs: $(progs)
tests: $(testprogs)
docs:
	cd doc/arch && make
	cd doc/userguide && make

clean:
	rm -f core $(progs) $(objs) *.a gateway.pid
	find . -name .cvsignore | xargs rm -f

depend .depend:
	$(MKDEPEND) */*.c > .depend
include .depend

libgw.a: $(gwobjs)
	ar rc libgw.a $(gwobjs)
	$(RANLIB) libgw.a

libgwlib.a: $(libobjs)
	ar rc libgwlib.a $(libobjs)
	$(RANLIB) libgwlib.a

libtest.a: $(testobjs)
	ar rc libtest.a $(testobjs)
	$(RANLIB) libtest.a

$(progs): libgw.a libgwlib.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $@.o libgw.a libgwlib.a $(LIBS)

$(testprogs): libtest.a libgw.a libgwlib.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $@.o libtest.a libgw.a libgwlib.a $(LIBS)

cvsignore:
	find . -name CVS -type d | sed 's:/CVS$$::' | \
		while read d; do > $$d/.cvsignore; \
		echo .cvsignore >> $$d/.cvsignore; done
	echo .depend >> .cvsignore
	for prog in $(progs) $(testprogs); do \
		echo `basename $$prog` >> `dirname $$prog`/.cvsignore; done
	cd doc/arch && $(MAKE) cvsignore
	cd doc/userguide && $(MAKE) cvsignore

install: all
	mkdir -p $(bindir)
	install $(progs) $(bindir)
