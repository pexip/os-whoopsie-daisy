VERSION=$(shell dpkg-parsechangelog -l../../debian/changelog | grep ^Version: | cut -d' ' -f2)
CFLAGS=$(shell pkg-config --cflags gio-2.0 glib-2.0 libcurl) \
		$(shell libgcrypt-config --cflags) \
		-g -I../../lib -I.. -DVERSION=\"$(VERSION)\" -DTEST -DTEST_DIR=\"$(CURDIR)\" -Wall
LIBS=$(shell pkg-config --libs gio-2.0 glib-2.0 libcurl) -lcap \
		$(shell libgcrypt-config --libs)

test_parse_report_SOURCES=test_parse_report.c \
							../whoopsie.c \
							../utils.c \
							../../lib/bson/bson.c \
							../../lib/bson/encoding.c \
							../../lib/bson/numbers.c
test_parse_report_EXECUTABLE=test_parse_report
test_parse_report_OBJECTS=$(test_parse_report_SOURCES:.c=.test.o)

test_utils_SOURCES=test_utils.c \
					../utils.c
test_utils_EXECUTABLE=test_utils
test_utils_OBJECTS=$(test_utils_SOURCES:.c=.test.o)

test_monitor_SOURCES=test_monitor.c \
					../monitor.c \
					../utils.c
test_monitor_EXECUTABLE=test_monitor
test_monitor_OBJECTS=$(test_monitor_SOURCES:.c=.test.o)

.PHONY: all clean check

all: check

check: $(test_parse_report_EXECUTABLE) $(test_utils_EXECUTABLE) $(test_monitor_EXECUTABLE)
	$(foreach p, $^, ./$p -k;)

$(test_parse_report_EXECUTABLE): $(test_parse_report_OBJECTS)
	c99 $^ $(LIBS) -o $@
$(test_utils_EXECUTABLE): $(test_utils_OBJECTS)
	c99 $^ $(LIBS) -o $@
$(test_monitor_EXECUTABLE): $(test_monitor_OBJECTS)
	c99 $^ $(LIBS) -o $@

test_parse_report_coverage: $(test_parse_report_OBJECTS:.test.o=.coverage.o)
	c99 $^ $(LIBS) -lgcov -o $@
test_utils_coverage: $(test_utils_OBJECTS:.test.o=.coverage.o)
	c99 $^ $(LIBS) -lgcov -o $@
test_monitor_coverage: $(test_monitor_OBJECTS:.test.o=.coverage.o)
	c99 $^ $(LIBS) -lgcov -o $@
coverage: test_parse_report_coverage test_utils_coverage test_monitor_coverage
	$(foreach p, $^, ./$p -k;)
	$(foreach p, $(wildcard ../*.c), gcov $p -o $(p:.c=.coverage.o);)

clean:
	rm -f $(test_parse_report_EXECUTABLE) \
		$(test_parse_report_OBJECTS) \
		$(test_utils_OBJECTS) \
		$(test_utils_EXECUTABLE) \
		$(test_monitor_OBJECTS) \
		$(test_monitor_EXECUTABLE) \
		test_parse_report_coverage \
		test_utils_coverage \
		coverage
	find ../.. \( -name '*.coverage.o' -o \
		-name '*.gcda' -o \
		-name '*.gcno' -o \
		-name '*.gcov' \) -delete

%.coverage.o: %.c
	c99 -c -o $@ $^ $(CFLAGS) --coverage -O0
%.test.o: %.c
	c99 -c -o $@ $^ $(CFLAGS)
