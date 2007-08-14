include config.mk

.PHONY: default all clean distclean install tags

default: all

all:
	for t in $(MODULES) $(EXECUTABLES) ; do \
		$(MAKE) -C .$$t -f $(SRCDIR)/build/$$t.mk $@ || exit 1 ; \
	done

clean:
	for t in $(MODULES) $(EXECUTABLES) ; do \
		$(MAKE) -C .$$t -f $(SRCDIR)/build/$$t.mk $@ ; \
	done

distclean: clean
	for t in $(MODULES) $(EXECUTABLES) ; do \
		rm -r .$$t ; \
	done
	rm -f config.mk config.h
	rm -f tags

install:
	for t in $(EXECUTABLES) ; do \
		install -m 755 -D $$t$(EXESUFFIX) $(DESTDIR)$(BINDIR)/$$t$(EXESUFFIX) ; \
	done
	for t in $(MODULES) ; do \
		install -m 755 -D $$t$(LIBSUFFIX) $(DESTDIR)$(REFDIR)/$$t$(LIBSUFFIX) ; \
	done
	install -m 644 -D $(SRCDIR)/q2pro.6 $(DESTDIR)$(MANDIR)/q2pro.6

tarball:
	mkdir -p baseq2pro
	mkdir -p openffa
	$(STRIP) q2pro.exe q2proded.exe
	$(STRIP) -o baseq2pro/ref_soft.dll ref_soft.dll 
	$(STRIP) -o baseq2pro/ref_gl.dll ref_gl.dll 
	$(STRIP) -o baseq2pro/mod_ui.dll mod_ui.dll 
	$(STRIP) -o openffa/gamex86.dll gamex86.dll 
	zip -9 ../q2pro-r${REVISION}-win32.zip q2pro.exe q2proded.exe baseq2pro/ref_soft.dll baseq2pro/ref_gl.dll baseq2pro/mod_ui.dll openffa/gamex86.dll
	rm -r baseq2pro
	rm -r openffa

tags:
	ctags $(SRCDIR)/source/*.[ch] $(SRCDIR)/source/openffa/*.[ch]
