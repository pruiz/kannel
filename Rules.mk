# Common makefile stuff.

# Uncomment the following for Debian GNU/Linux
HTML_DSL = /usr/lib/sgml/stylesheet/dsssl/docbook/nwalsh/html/docbook.dsl
TEX_DSL = /usr/lib/sgml/stylesheet/dsssl/docbook/nwalsh/print/docbook.dsl
# XML_DECL = not used at the moment

# Uncomment the following for Red Hat Linux
#HTML_DSL = /usr/lib/sgml/stylesheets/nwalsh-modular/html/docbook.dsl
#TEX_DSL = /usr/lib/sgml/stylesheets/nwalsh-modular/print/docbook.dsl

.xml.html:
	sed "s/#FIGTYPE#/.png/;\
		s/#DATE#/`date -r $*.timestamp +'%B %e, %Y'`/" $< > temp.xml
	jade -V nochunks -t sgml -d $(HTML_DSL) $(XML_DECL) temp.xml > $@
	set -e; if grep '<!DOCTYPE book' $< >/dev/null; then \
		    case $< in /*) file=$<;; *) file=`pwd`/$<;; esac; \
		    rm -rf $*; \
		    mkdir $*; \
		    cd $*; \
		    jade -t sgml -d $(HTML_DSL) $(XML_DECL) ../temp.xml; \
		    ln -s book1.htm* index.html; \
		    cd ..; \
		    if test "$(figspng)"; then cp $(figspng) $*; fi; \
		fi
	rm -f temp.xml

.xml.ps:
	sed "s/#FIGTYPE#/.ps/;\
		s/#DATE#/`date -r $*.timestamp +'%B %e, %Y'`/" $< > temp.xml
	jade -o $*.tex -t tex -d $(TEX_DSL) $(XML_DECL) temp.xml
	jadetex $*.tex >/dev/null
	jadetex $*.tex >/dev/null
	jadetex $*.tex >/dev/null
	dvips -q -o $@ $*.dvi
	rm -f $*.dvi $*.tex $*.aux $*.log temp.xml

.fig.png:
	fig2dev -Lpng $< $@

.fig.ps:
	fig2dev -Lps $< $@

.SUFFIXES: $(SUFFIXES) .xml .html .ps .fig .png

docsps = $(docs:.xml=.ps)
docshtml = $(docs:.xml=.html)
figspng = $(figs:.fig=.png)
figsps = $(figs:.fig=.ps)
