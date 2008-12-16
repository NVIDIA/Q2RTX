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

uninstall:
	echo "Single user mode configured, can't uninstall" && exit 1

else # SINGLEUSER

install:
	for t in $(EXECUTABLES) ; do \
		install -m 755 -D $$t $(DESTDIR)$(BINDIR)/$$t ; \
	done
	for t in $(LIBRARIES) ; do \
		install -m 755 -D $$t $(DESTDIR)$(LIBDIR)/$$t ; \
	done
	install -m 644 -D $(SRCDIR)/q2pro.6 $(DESTDIR)$(MANDIR)/q2pro.6
	install -m 644 -D $(SRCDIR)/wiki/doc/q2pro.menu \
		$(DESTDIR)$(DATADIR)/baseq2/q2pro.menu
	install -m 644 -D $(SRCDIR)/source/q2pro.desktop \
		$(DESTDIR)$(APPDIR)/q2pro.desktop
	install -m 644 -D $(SRCDIR)/source/q2pro.xpm \
		$(DESTDIR)$(PIXDIR)/q2pro.xpm

uninstall:
	for t in $(EXECUTABLES) ; do \
		rm -f $(DESTDIR)$(BINDIR)/$$t ; \
	done
	for t in $(LIBRARIES) ; do \
		rm -f $(DESTDIR)$(LIBDIR)/$$t ; \
	done
	rm -f $(DESTDIR)$(MANDIR)/q2pro.6
	rm -f $(DESTDIR)$(DATADIR)/baseq2/q2pro.menu
	rm -f $(DESTDIR)$(APPDIR)/q2pro.desktop
	rm -f $(DESTDIR)$(PIXDIR)/q2pro.xpm

endif # !SINGLEUSER

strip:
	for t in $(BINARIES) ; do \
		$(STRIP) $$t ; \
	done

docs:
	$(MAKE) -C wiki

tags:
	ctags $(SRCDIR)/source/*.[ch] $(SRCDIR)/source/openffa/*.[ch]

