## processed by automake, together with configure.in, to produce Makefile.in
#
# Top level Makefile.am for WapIT (WAP/SMS) Gateway.
#

EXTRA_DIST = \
	Makefile.orig \
	README.autoconf README.docbook README.src \
	Release-0.1 Rules.mk \
	doc test utils

SUBDIRS = gwlib gw test

cvsignore:
	find . -name CVS -type d | sed 's:/CVS$$::' | \
		while read d; do \
			echo '*~' > $$d/.cvsignore ; \
			echo .cvsignore >> $$d/.cvsignore; \
		done
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir cvsignore ; \
	done
	( \
		echo Makefile ; \
		echo config.cache ; \
		echo config.h ; \
		echo config.log ; \
		echo config.status ; \
		echo stamp-h ; \
	) >>.cvsignore

