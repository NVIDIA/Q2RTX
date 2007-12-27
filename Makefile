include config.mk

.PHONY: default all clean distclean install strip tags

default: all

all:
	for t in $(TARGETS) ; do \
		$(MAKE) -C .$$t -f $(SRCDIR)/build/$$t.mk $@ || exit 1 ; \
	done

clean:
	for t in $(TARGETS) ; do \
		$(MAKE) -C .$$t -f $(SRCDIR)/build/$$t.mk $@ ; \
	done

distclean: clean
	for t in $(TARGETS) ; do \
		rm -r .$$t ; \
	done
	rm -f config.mk config.h
	rm -f tags

ifndef SINGLEUSER

install:
	for t in $(EXECUTABLES) ; do \
		install -m 755 -D $$t $(DESTDIR)$(BINDIR)/$$t ; \
	done
	for t in $(LIBRARIES) ; do \
		install -m 755 -D $$t $(DESTDIR)$(REFDIR)/$$t ; \
	done
	install -m 644 -D $(SRCDIR)/q2pro.6 $(DESTDIR)$(MANDIR)/q2pro.6

endif # SINGLEUSER

strip:
	for t in $(BINARIES) ; do \
		$(STRIP) $$t ; \
	done

doc:
	$(MAKE) -C wiki

tags:
	ctags $(SRCDIR)/source/*.[ch] $(SRCDIR)/source/openffa/*.[ch]

