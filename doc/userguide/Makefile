docs = userguide.xml
subdocs = # $(shell echo userguide-*.xml)

figs = 

instdir = $(HOME)/public_html/wapit-intra/userguide

# You should not need to touch anything below this.

include ../../Rules.mk

all: figs html ps
html: figs $(docshtml)
ps: figs $(docsps)
figs: $(figspng) $(figsps)

userguide.html: userguide.timestamp $(docs) $(subdocs)
userguide.ps: userguide.timestamp $(docs) $(subdocs)

userguide.timestamp: $(docs) $(subdocs) $(figs)
	touch -r `ls -1 -t $(docs) $(subdocs) $(figs)` $@

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
	rm -rf $(instdir)/userguide
	mkdir -p $(instdir)/userguide
	cp $(docs) $(docsps) $(docshtml) $(figspng) $(figsps) \
		$(instdir)/userguide
	for dir in $(docs:.xml=); do \
		if [ -d $$dir ]; then cp -r $$dir $(instdir)/userguide; fi; done
	chmod -R go=u,go-w $(instdir)/userguide
