## processed by automake, together with configure.in, to produce Makefile.in
#
# Makefile for WapIT (WAP/SMS) Gateway.
#

INCLUDES = -DHAVE_THREADS=1 -I../gwlib
LDADD = libgw.a ../gwlib/libgwlib.a -lpthread

bin_PROGRAMS = bearerbox smsbox wapbox
noinst_LIBRARIES = libgw.a

noinst_HEADERS = \
	alt_charsets.h bb.h bb_msg.h boxc.h cgi.h csdr.h html.h \
	msg-decl.h msg.h sms_msg.h smsbox_req.h smsc.h smsc_p.h \
	smsc_smpp.h urltrans.h wapbox.h wml.h wsp.h wsp_events-decl.h \
	wsp_machine-decl.h wsp_state-decl.h wtp.h wtp_events-decl.h \
	wtp_machine-decl.h wtp_send.h wtp_state-decl.h wtp_timer.h

libgw_a_SOURCES = \
	bb_msg.c boxc.c cgi.c csdr.c html.c msg.c \
	sms_msg.c smsbox_req.c \
	smsc.c smsc_cimd.c smsc_emi.c smsc_fake.c smsc_smpp.c \
	urltrans.c wml.c wsp.c wtp.c wtp_send.c wtp_timer.c


## bearerbox_SOURCES = bearerbox.c
## smsbox_SOURCES = smsbox.c
## wapbox_SOURCES = wapbox.c

cvsignore:
	( \
		echo Makefile ; \
	) >>.cvsignore

