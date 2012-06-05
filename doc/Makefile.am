MANDEFS  = murphy-audio.x
MANUALS  = $(MANDEFS:.x=)
TARGETS  =
DOCFILES =

vpath %.xml murphy-audio/db

include $(PA_MAKE_DOCRULES)


if BUILD_DOCUMENTATION
DOCDIRS = murphy-audio
endif

if BUILD_PDF_DOCUMENTS
TARGETS += pdf_targets
DOCFILES += $(MANDEFS:.x=.pdf)
endif


SUBDIRS = $(DOCDIRS)


all-am: $(TARGETS)

pdf_targets: murphy-audio.pdf

murphy-audio.pdf: murphy-audio.xml

clean-local:
	rm -f *.pdf scripts/*~

