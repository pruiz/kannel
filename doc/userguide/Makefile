docs = userguide.xml
subdocs = # $(shell echo userguide-*.xml)

figs = 

instdir = $(HOME)/public_html/wapit-intra/userguide

# You should not need to touch anything below this.

include ../../Rules.mk

userguide.html: userguide.timestamp $(docs) $(subdocs)
userguide.ps: userguide.timestamp $(docs) $(subdocs)

$(docs:.xml=.timestamp): $(docs) $(subdocs) $(figs)
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
	rm -rf $(instdir)
	mkdir -p $(instdir)
	cp index.html $(docs) $(docsps) $(docshtml) $(figspng) $(figsps) \
		$(instdir)
	for dir in $(docs:.xml=); do \
		if [ -d $$dir ]; then cp -r $$dir $(instdir); fi; done
	chmod -R go=u,go-w $(instdir)
