
all-local:
	@pushd lyx ; \
	lyxfiles=`echo *.lyx` ;\
	for lf in $$lyxfiles ; do \
	    xf=$${lf/.lyx/.xml} ; \
	    rm -f $$xf; \
	    echo "export $$xf"; \
	    $(PD_LYX) --export docbook-xml $$lf ; \
	done ; \
	figfiles=`echo *.fig` ;\
	for fig in $$figfiles ; do \
	    pdf=$${fig/.fig/.pdf} ; \
	    png=$${fig/.fig/.png} ; \
	    rm -f $$pdf $$png ; \
	    echo "export $$fig to pdf and png"; \
	    $(PD_FIG2DEV) -L pdf -m 0.5 $$fig $$pdf ; \
	    $(PD_FIG2DEV) -L png $$fig $$png ; \
	done ; \
	popd ;\
	TOPDIR=$(abs_top_srcdir) $(PD_DOXYGEN) Doxyfile;\
	doxydir=doxygen ; \
	doxmldir=$$doxydir/xml ; \
	mkdir -p $$doxmldir ; \
	dxfiles=`cd $$doxmldir; echo *_8c.xml`; \
	for df in $$dxfiles ; do \
	    module=`echo $$df | sed -e s/_8c.xml//`; \
	    echo "xsltproc $$module"; \
	    $(PD_XSLTPROC) $(PD_DOXML2DB_STY) $$doxmldir/$$df \
	         > $${doxydir}/pulse_$${module}.xml ; \
	done ; \
	rm -rf $$doxmldir


all-clean:
	rm -rf lyx/*.xml doxygen/*.xml