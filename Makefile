include config.mk

.PHONY: default all binary strip tags clean distclean

default: all
all: binary

%-binary:
	$(MAKE) -C .$* -f $(SRCDIR)/build/$*.mk binary

%-strip:
	$(MAKE) -C .$* -f $(SRCDIR)/build/$*.mk strip

%-clean:
	$(MAKE) -C .$* -f $(SRCDIR)/build/$*.mk clean

binary: $(patsubst %,%-binary,$(TARGETS))

strip: $(patsubst %,%-strip,$(TARGETS))

tags:
	ctags $(SRCDIR)/src/*.[ch] $(SRCDIR)/src/baseq2/*.[ch]

clean: $(patsubst %,%-clean,$(TARGETS))

distclean: clean
	rm -rf .q2pro .q2proded .baseq2
	rm -f config.mk config.h
	rm -f tags

ifndef SINGLEUSER
.PHONY: install uninstall

%-install:
	$(MAKE) -C .$* -f $(SRCDIR)/build/$*.mk install

%-uninstall:
	$(MAKE) -C .$* -f $(SRCDIR)/build/$*.mk uninstall

install: $(patsubst %,%-install,$(TARGETS))
	install -m 644 -D $(SRCDIR)/src/q2pro.default \
		$(DESTDIR)$(SITECFG)

uninstall: $(patsubst %,%-uninstall,$(TARGETS))
	-rm $(DESTDIR)$(SITECFG)
endif

