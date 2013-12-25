VERSION=$(shell dpkg-parsechangelog | grep ^Version: | cut -d' ' -f2)
CFLAGS=$(shell pkg-config --cflags gio-2.0 glib-2.0 libcurl) $(shell libgcrypt-config --cflags) -g -Ilib -Wall -Werror -Os -DVERSION=\"$(VERSION)\"
LIBS=$(shell pkg-config --libs gio-2.0 glib-2.0 libcurl) $(shell libgcrypt-config --libs) -lcap
SOURCES=src/whoopsie.c src/utils.c src/connectivity.c src/monitor.c lib/bson/bson.c lib/bson/encoding.c lib/bson/numbers.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=src/whoopsie
BIN=$(DESTDIR)/usr/bin
DATA=$(DESTDIR)/etc

.PHONY: all clean check install

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	c99 $(OBJECTS) $(LIBS) -o $@
clean:
	rm -f $(EXECUTABLE) $(OBJECTS)
	$(MAKE) -C src/tests clean
check:
	$(MAKE) -C src/tests
	if type cppcheck >/dev/null 2>&1; then \
		cppcheck . --error-exitcode=1; \
	fi
	find -name "*.py" -o -name "*.wsgi" | xargs pyflakes
coverage:
	$(MAKE) -C src/tests coverage
%.o: %.c
	c99 -c $(CFLAGS) -o $@ $^

install: all
	install -d $(BIN)
	install src/whoopsie $(BIN)
	install -d $(DATA)/init
	install -m644 data/whoopsie.conf $(DATA)/init
	install -d $(DATA)/default
	install -m644 data/whoopsie $(DATA)/default
	install -d $(DESTDIR)/usr/share/apport/package-hooks
	install -m644 data/whoopsie.py $(DESTDIR)/usr/share/apport/package-hooks
