CC      ?= cc
CFLAGS  ?= -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wshadow -O2 -g
DEFS    ?=
CPPFLAGS += -Iinclude -Ivendor $(DEFS)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
# _POSIX_C_SOURCE hides Darwin extensions such as _NSGetExecutablePath's headers
CPPFLAGS += -D_DARWIN_C_SOURCE
endif
LDFLAGS ?=
LDLIBS  ?= -lm

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share/fidocad

SRC := src/util.c src/sys.c src/parse.c src/emit.c src/lib.c src/tex.c src/font.c src/ttf.c src/vendor_impl.c src/render.c
OBJ := $(patsubst src/%.c,build/%.o,$(SRC))
CLI_OBJ := build/cli.o
TEST_OBJ := build/test_core.o

.PHONY: all test clean install sanitize

all: build/fcai

build:
	mkdir -p build

build/font.o: src/font_stroke.h
build/render.o: src/font8x8.h src/font_stroke.h src/font_math.h
build/tex.o: src/font8x8.h src/font_math.h

build/%.o: src/%.c include/fidocad.h src/internal.h | build
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

build/cli.o: src/cli.c include/fidocad.h src/internal.h | build
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

build/test_core.o: tests/test_core.c include/fidocad.h src/internal.h | build
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

build/fcai: $(OBJ) $(CLI_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

build/test_core: $(OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

test: build/test_core build/fcai
	./build/test_core
	./build/fcai validate tests/fixtures/full-pattern.fcd
	./build/fcai describe tests/fixtures/full-pattern.fcd >/dev/null
	./build/fcai json tests/fixtures/full-pattern.fcd >/dev/null
	./build/fcai --no-stdlib validate tests/fixtures/edge-cases.fcd

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O1 -g -fsanitize=address,undefined" \
		LDFLAGS="-fsanitize=address,undefined" test

install: build/fcai
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(DATADIR)/lib $(DESTDIR)$(DATADIR)/fonts
	install -m 755 build/fcai $(DESTDIR)$(BINDIR)/fcai
	install -m 644 share/lib/*.fcl $(DESTDIR)$(DATADIR)/lib/
	install -m 644 share/fonts/*.ttf $(DESTDIR)$(DATADIR)/fonts/

clean:
	rm -rf build
