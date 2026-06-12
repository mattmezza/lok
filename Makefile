# mlock - a pretty screen locker
# See LICENSE file for copyright and license details.

include config.mk

SRC = mlock.c
OBJ = $(SRC:.c=.o)

all: mlock

.c.o:
	$(CC) -c $(CFLAGS) $<

$(OBJ): arg.h config.h config.mk

config.h:
	cp config.def.h $@

mlock: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f mlock $(OBJ) mlock-$(VERSION).tar.gz test/shim.so

dist: clean
	mkdir -p mlock-$(VERSION)
	cp -R LICENSE Makefile README.md config.mk config.def.h arg.h \
		mlock.1 $(SRC) mlock-$(VERSION)
	tar -cf - mlock-$(VERSION) | gzip > mlock-$(VERSION).tar.gz
	rm -rf mlock-$(VERSION)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f mlock $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/mlock
	chmod u+s $(DESTDIR)$(PREFIX)/bin/mlock
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < mlock.1 > $(DESTDIR)$(MANPREFIX)/man1/mlock.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/mlock.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/mlock
	rm -f $(DESTDIR)$(MANPREFIX)/man1/mlock.1

test: all
	sh test/run.sh

.PHONY: all clean dist install uninstall test
