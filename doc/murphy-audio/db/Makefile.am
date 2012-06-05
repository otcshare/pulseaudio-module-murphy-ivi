TOP_SRCDIR = $(abs_top_srcdir)/src

vpath %.lyx $(PA_DOCDIR)/murphy-audio/lyx

include $(PA_MAKE_DOCRULES)


FIGURES_SVG = components.svg
FIGURES_PNG = $(FIGURES_SVG:.svg=.png)
FIGURES_PDF = $(FIGURES_SVG:.svg=.pdf)

FIGURES = $(FIGURES_SVG) $(FIGURES_PNG) $(FIGURES_PDF)


TARGETS = $(FIGURES) murphy-audio.xml

xmldir = $(PA_DOCDIR)
nodist_xml_DATA = $(TARGETS)


all-am: $(TARGETS)
	for f in $(FIGURES_SVG) ; do \
	    echo "  CP    $$f" 1>&2 ; \
	    cp $(PA_FIGDIR)/$$f . ; \
	done


overview.xml: overview.lyx


murphy-audio.xml: murphy-audio.lyx \
	          overview.xml

clean-local:
	rm -f *~ $(TARGETS) \
                overview.xml \
		../lyx/*.xml ../lyx/*~
