docs = arch.xml
subdocs = $(shell echo arch-*.xml)

figs = arch-externals.fig arch-components.fig arch-bbox.fig

instdir = $(HOME)/public_html/wapit-intra/arch

# You should not need to touch anything below this.

include ../../Rules.mk

all: figs html ps
html: figs $(docshtml)
ps: figs $(docsps)
figs: $(figspng) $(figsps)

arch.html: arch.timestamp $(docs) $(subdocs)
arch.ps: arch.timestamp $(docs) $(subdocs)

arch.timestamp: $(docs) $(subdocs) $(figs)
	touch -r `ls -1 -t $(docs) $(subdocs) $(figs)` arch.timestamp

clean:
	rm -rf $(docsps) $(docshtml) $(figspng) $(figsps) $(docs:.xml=)
	rm -f arch.timestamp

cvsignore:
	rm -f .cvsignore
	echo .cvsignore >> .cvsignore
	echo arch.timestamp >> .cvsignore
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
