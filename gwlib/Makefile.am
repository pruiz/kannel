## processed by automake, together with configure.in, to produce Makefile.in
#
# Makefile for WapIT (WAP/SMS) Gateway.
#

INCLUDES = -DHAVE_THREADS=1

noinst_LIBRARIES = libgwlib.a

noinst_HEADERS = \
	getnameinfo.h gwstdint.h \
	conffile.h gwlib.h gwmem.h gwstr.h \
	http.h log.h octstr.h socket.h thread.h utils.h

libgwlib_a_SOURCES = \
	conffile.c gwmem.c gwstr.c http.c \
	log.c octstr.c socket.c thread.c utils.c

cvsignore:
	( \
		echo Makefile ; \
	) >>.cvsignore

