PACKAGE := pulseaudio-module-murphy-ivi
VERSION := 1.1.10
THISDIR := fedora

TOPDIR  := $(shell   while [ ! -d $(THISDIR) -a "`pwd`" != "/" ]; do \
                       cd ..; \
                     done; \
		     [ -d $(THISDIR) ] && pwd || exit 1)
RPMSDIR   = $(TOPDIR)/$(THISDIR)

ARCH      = $(shell rpm -q --queryformat "%{ARCH}\n" rpm)
SPECFILE := $(PACKAGE).spec
TARBALL  := $(PACKAGE)-$(VERSION).tar.gz

STAMP    := $(shell date +%Y-%m-%d-%H%M%S)
TMPDIR   := /tmp/$(PACKAGE)-$(USER)/$(STAMP)
PKGDIR   := $(PACKAGE)-$(VERSION)

# tarball type to use (git = HEAD, current = working copy)
type      = current

all: rpms #cleanup

#showvar:
#	@echo "$(var) = $($(var))"

# rule to build RPM(s) for this package
rpms: rmtar $(SPECFILE) tarball
	mkdir -p $(TMPDIR)/{BUILD,RPMS,SRPMS,SOURCES,SPECS} && \
	cp $(SPECFILE) $(TMPDIR)/SPECS && \
	cp $(TARBALL) $(TMPDIR)/SOURCES && \
	rpmbuild --define '_topdir $(TMPDIR)' \
	    -bb $(TMPDIR)/SPECS/$(PACKAGE).spec && \
	rpmbuild --define '_topdir $(TMPDIR)' \
	    -bs $(TMPDIR)/SPECS/$(PACKAGE).spec && \
	cp $(TMPDIR)/RPMS/*/*.rpm $(RPMSDIR) && \
	cp $(TMPDIR)/SRPMS/*.rpm $(RPMSDIR)


cleanup:
	rm -fr $(TMPDIR) && \
	rm -f $(SPECFILE)
	rm -f $(TARBALL)

# rules to patch the SPEC file and build a source tarball
$(SPECFILE): $(SPECFILE).in
	cat $< | \
	    sed 's/##__VERSION__##/$(VERSION)/g;s/##__TARDIR__##/$(TARDIR)/g'| \
	    sed 's/##__ARCH__##/$(ARCH)/g' \
	    > $@

tarball: $(type)-tarball

git-tarball:
	cd .. && \
	    git archive --format=tar --prefix=$(PACKAGE)-$(VERSION)/ HEAD | \
	        gzip > $(THISDIR)/$(TARBALL)

current-tarball:
	cd .. && make distclean || : && ./autogen.sh
	cd .. && mkdir -p $(TMPDIR)/$(PKGDIR) && \
	tar -cvf - --exclude '$(THISDIR)' \
	           --exclude '.git' . | \
	tar -C $(TMPDIR)/$(PKGDIR) -xvf - && \
	tar -C $(TMPDIR) -cvzf $(THISDIR)/$(TARBALL) $(PKGDIR) && \
	rm -fr $(TMPDIR)

rpm-export: $(SPECFILE) $(TARBALL)

rmtar:
	rm -f $(TARBALL)
