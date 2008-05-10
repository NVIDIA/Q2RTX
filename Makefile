include config.mk

.PHONY: default all binaries clean distclean install strip tags

default: all
all: binaries

binaries:
	for t in $(TARGETS) ; do \
		$(MAKE) -C .$$t -f $(SRCDIR)/build/$$t.mk all || exit 1 ; \
	done

clean:
	for t in $(TARGETS) ; do \
		$(MAKE) -C .$$t -f $(SRCDIR)/build/$$t.mk clean ; \
	done

distclean: clean
	for t in $(TARGETS) ; do \
		rm -r .$$t ; \
	done
	rm -f config.mk config.h
	rm -f tags

ifdef SINGLEUSER
install:
	echo "Single user mode configured, can't install" && exit 1
else
install:
	for t in $(EXECUTABLES) ; do \
		install -m 755 -D $$t $(DESTDIR)$(BINDIR)/$$t ; \
	done
	for t in $(LIBRARIES) ; do \
		install -m 755 -D $$t $(DESTDIR)$(REFDIR)/$$t ; \
	done
	install -m 644 -D $(SRCDIR)/q2pro.6 $(DESTDIR)$(MANDIR)/q2pro.6
	install -m 644 -D $(SRCDIR)/wiki/doc/q2pro.menu \
		$(DESTDIR)$(DATADIR)/baseq2/q2pro.menu
endif # SINGLEUSER

strip:
	for t in $(BINARIES) ; do \
		$(STRIP) $$t ; \
	done

docs:
	$(MAKE) -C wiki

tags:
	ctags $(SRCDIR)/source/*.[ch] $(SRCDIR)/source/openffa/*.[ch]

