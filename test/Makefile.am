## processed by automake, together with configure.in, to produce Makefile.in
#
# Makefile for WapIT (WAP/SMS) Gateway.
#

INCLUDES = -DHAVE_THREADS=1 -I../gwlib -I../gw
LDADD = ../gw/libgw.a ../gwlib/libgwlib.a -lpthread

bin_PROGRAMS = fakesmsc test_msg test_wml udp_send wapbox_feeder

cvsignore:
	( \
		echo Makefile ; \
	) >>.cvsignore

