# lok - a pretty screen locker
# See LICENSE file for copyright and license details.

include config.mk

SRC = lok.c fingerprint.c fingerprint-pam.c
OBJ = $(SRC:.c=.o)

all: lok

.c.o:
	$(CC) -c $(CFLAGS) $<

$(OBJ): arg.h config.h config.mk fingerprint.h

config.h:
	cp config.def.h $@

lok: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

lok-static: lok.c fingerprint.c static-loader.c arg.h config.h config.mk fingerprint.h
	$(CC) $(CFLAGS) -DLOK_PAM_HELPER=\"$(PREFIX)/libexec/lok-pam\" -static \
		-Wl,--wrap=dlopen -o $@ lok.c fingerprint.c static-loader.c $(STATIC_LIBS)

lok-pam: fingerprint-pam.c config.mk fingerprint.h
	$(CC) $(CFLAGS) -DLOK_PAM_MAIN -o $@ fingerprint-pam.c -lpam

clean:
	rm -f config.h
	rm -f lok lok-static lok-pam $(OBJ) lok-$(VERSION).tar.gz test/shim.so test/fingerprint

dist: clean
	mkdir -p lok-$(VERSION)
	cp -R LICENSE Makefile README.md config.mk config.def.h arg.h \
		lok.1 fingerprint.h static-loader.c pam.d test $(SRC) lok-$(VERSION)
	tar -cf - lok-$(VERSION) | gzip > lok-$(VERSION).tar.gz
	rm -rf lok-$(VERSION)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -o root -g root -m 4755 lok $(DESTDIR)$(PREFIX)/bin/lok
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < lok.1 > $(DESTDIR)$(MANPREFIX)/man1/lok.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/lok.1

install-pam:
	install -d $(DESTDIR)/etc/pam.d
	install -o root -g root -m 644 pam.d/lok-fingerprint $(DESTDIR)/etc/pam.d/lok-fingerprint

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/lok
	rm -f $(DESTDIR)$(MANPREFIX)/man1/lok.1

test: all
	$(CC) $(CFLAGS) -o test/fingerprint test/fingerprint.c $(shell $(PKG_CONFIG) --libs gio-2.0)
	./test/fingerprint
	sh test/run.sh

release:
	@set -eu; tag=$${name:-}; \
	case "$$tag" in *[!v0-9.]*) tag= ;; esac; \
	printf '%s\n' "$$tag" | LC_ALL=C grep -Exq 'v(0|[1-9][0-9]*)(\.(0|[1-9][0-9]*)){2}' \
		|| { echo 'Usage: make release name=v0.1.0' >&2; exit 1; }; \
	test -z "$$(git status --porcelain)" || { echo 'Commit the release changes first.' >&2; exit 1; }; \
	git push origin "HEAD:refs/tags/$$tag"; \
	if draft=$$(gh release view "$$tag" --json isDraft --jq .isDraft 2>/dev/null); then \
		test "$$draft" = true || { echo "Release $$tag is already published." >&2; exit 1; }; \
	else \
		gh release create "$$tag" --verify-tag --draft --generate-notes --title "lok $$tag"; \
	fi; \
	gh workflow run release.yml --ref "$$tag"

.PHONY: all clean dist install install-pam uninstall test release
