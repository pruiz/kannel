docs = userguide.xml
subdocs = # $(shell echo userguide-*.xml)

figs = 

instdir = $(HOME)/public_html/wapit-intra/userguide

# You should not need to touch anything below this.

HTML_DSL = /usr/lib/sgml/stylesheet/dsssl/docbook/nwalsh/html/docbook.dsl
TEX_DSL = /usr/lib/sgml/stylesheet/dsssl/docbook/nwalsh/print/docbook.dsl
XML_DECL = /usr/lib/sgml/declaration/xml.decl

.xml.html:
	sed "s/#FIGTYPE#/.png/;\
		s/#DATE#/`date -r userguide.timestamp +'%B %e, %Y'`/" $< > temp.xml
	jade -V nochunks -t sgml -d $(HTML_DSL) $(XML_DECL) temp.xml > $@
	set -e; if grep '<!DOCTYPE book' $< >/dev/null; then \
		    case $< in /*) file=$<;; *) file=`pwd`/$<;; esac; \
		    rm -rf $*; \
		    mkdir $*; \
		    cd $*; \
		    jade -t sgml -d $(HTML_DSL) $(XML_DECL) ../temp.xml; \
		    ln -s book1.html index.html; \
		    cd ..; \
		    if test "$(figspng)"; then cp $(figspng) $*; fi; \
		fi
	rm -f temp.xml

.xml.ps:
	sed "s/#FIGTYPE#/.ps/;\
		s/#DATE#/`date -r userguide.timestamp +'%B %e, %Y'`/" $< > temp.xml
	jade -o $*.tex -t tex -d $(TEX_DSL) $(XML_DECL) temp.xml
	jadetex $*.tex >/dev/null
	jadetex $*.tex >/dev/null
	jadetex $*.tex >/dev/null
	dvips -q -o $@ $*.dvi
	rm -f $*.dvi $*.tex $*.aux $*.log temp.xml

.fig.png:
	fig2dev -Lpng $< > $@

.fig.ps:
	fig2dev -Lps $< > $@

.SUFFIXES: $(SUFFIXES) .xml .html .ps .fig .png

docsps = $(docs:.xml=.ps)
docshtml = $(docs:.xml=.html)
figspng = $(figs:.fig=.png)
figsps = $(figs:.fig=.ps)

all: figs html ps
html: figs $(docshtml)
ps: figs $(docsps)
figs: $(figspng) $(figsps)

userguide.html: userguide.timestamp $(docs) $(subdocs)
userguide.ps: userguide.timestamp $(docs) $(subdocs)

userguide.timestamp: $(docs) $(subdocs) $(figs)
	touch -r `ls -1 -t $(docs) $(subdocs) $(figs)` userguide.timestamp

clean:
	rm -rf $(docsps) $(docshtml) $(figspng) $(figsps) $(docs:.xml=)
	rm -f *.timestamp

cvsignore:
	rm -f .cvsignore
	echo .cvsignore >> .cvsignore
	echo userguide.timestamp >> .cvsignore
	for file in $(docsps) $(docshtml) $(figspng) $(figsps) $(docs:.xml=); \
		do echo $$file >> .cvsignore; done

public: all
	rm -rf $(instdir)
	mkdir -p $(instdir)
	cp index.html $(docs) $(docsps) $(docshtml) $(figspng) $(figsps) \
		$(instdir)
	for dir in $(docs:.xml=); do \
		if [ -d $$dir ]; then cp -r $$dir $(instdir); fi; done
	chmod -R go=u,go-w $(instdir)
