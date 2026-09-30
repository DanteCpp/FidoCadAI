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

SRC := src/util.c src/sys.c src/parse.c src/emit.c src/edit.c src/lint.c src/lib.c src/tex.c src/font.c src/ttf.c src/vendor_impl.c src/render.c
OBJ := $(patsubst src/%.c,build/%.o,$(SRC))
CLI_OBJ := build/cli.o
TEST_OBJ := build/test_core.o

# Optional Qt6 GUI (fcai-gui). The core library and fcai stay dependency-free;
# the GUI is built only when Qt6 Widgets + Network are available. Works with
# Linux (qt6-base-dev) and macOS (brew install qt pkg-config).
CXX      ?= c++
# -Wno-sfinae-incomplete is a GCC flag (GCC 13 false positive with Qt); clang
# (macOS) does not know it, so only pass it when the compiler is not clang.
CXX_IS_CLANG := $(findstring clang,$(shell $(CXX) --version 2>/dev/null))
CXXFLAGS ?= -std=c++17 -Wall -Wextra $(if $(CXX_IS_CLANG),,-Wno-sfinae-incomplete) -O2 -g
QT_PKGS  := Qt6Widgets
HAVE_QT  := $(shell pkg-config --exists $(QT_PKGS) 2>/dev/null && echo 1)
HAVE_QT_TEST := $(shell pkg-config --exists Qt6Test 2>/dev/null && echo 1)
QT_CFLAGS = $(shell pkg-config --cflags $(QT_PKGS))
QT_LIBS   = $(shell pkg-config --libs $(QT_PKGS))
GUI_SRC  := gui/main.cpp gui/window.cpp gui/canvas.cpp gui/session.cpp gui/document.cpp gui/settings.cpp gui/theme.cpp gui/props.cpp
GUI_OBJ  := $(patsubst gui/%.cpp,build/gui/%.o,$(GUI_SRC))
GUI_HDR  := $(wildcard gui/*.h) include/fidocad.h

.PHONY: all test test-gui clean install install-skill sanitize gui system-install system-update

all: build/fcai $(if $(HAVE_QT),build/fcai-gui)

gui: build/fcai-gui

build/gui:
	mkdir -p build/gui

build/gui/%.o: gui/%.cpp $(GUI_HDR) | build/gui
	$(CXX) $(CXXFLAGS) -fPIC $(CPPFLAGS) $(QT_CFLAGS) -c $< -o $@

build/fcai-gui: $(OBJ) $(GUI_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(QT_LIBS) $(LDLIBS)

build/gui/test_gui.o: tests/test_gui.cpp $(GUI_HDR) | build/gui
	$(CXX) $(CXXFLAGS) -fPIC $(CPPFLAGS) $(QT_CFLAGS) $(shell pkg-config --cflags Qt6Test) -c $< -o $@

build/test_gui: $(OBJ) $(filter-out build/gui/main.o,$(GUI_OBJ)) build/gui/test_gui.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(QT_LIBS) $(shell pkg-config --libs Qt6Test) $(LDLIBS)

# LeakSanitizer flags fontconfig/pango caches owned by Qt, not by us.
test-gui: build/test_gui build/fcai-gui build/fcai
	QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=0 ./build/test_gui

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
	sh tests/test_cli.sh ./build/fcai
	./build/fcai validate tests/fixtures/full-pattern.fcd
	./build/fcai describe tests/fixtures/full-pattern.fcd >/dev/null
	./build/fcai json tests/fixtures/full-pattern.fcd >/dev/null
	./build/fcai --no-stdlib validate tests/fixtures/edge-cases.fcd
	./build/fcai diff tests/fixtures/full-pattern.fcd tests/fixtures/full-pattern.fcd | grep -q "no changes"
	./build/fcai skill | grep -q "fcai session wait"
	@d=$$(mktemp -d); \
	  ./build/fcai session set $$d grid 10 && \
	  grep -q '"key":"grid"' $$d/commands.jsonl && \
	  printf '[FIDOCAD]\n' | ./build/fcai session push $$d - && \
	  grep -q '"cmd":"push"' $$d/commands.jsonl && \
	  ./build/fcai session ack $$d "got it" && \
	  grep -q '"cmd":"ack"' $$d/commands.jsonl && \
	  echo '{"event":"send","note":"x"}' > $$d/events.jsonl && \
	  ./build/fcai session wait $$d --timeout 2 | grep -q '"note":"x"' && \
	  ./build/fcai session path tests/fixtures/full-pattern.fcd | grep -q fcai-session; \
	  rc=$$?; rm -rf $$d; exit $$rc
	@d=$$(mktemp -d); \
	  printf '[FIDOLIB my]\n[aaa test]\nLI 100 100 110 100\n' > $$d/parts.fcl && \
	  FIDO_USER_LIB_DIR=$$d ./build/fcai config | grep -q "user_lib = $$d" && \
	  FIDO_USER_LIB_DIR=$$d ./build/fcai lib | grep -q 'parts.aaa'; \
	  rc=$$?; rm -rf $$d; exit $$rc
	@d=$$(mktemp -d); \
	  mkdir -p $$d/fidocad && printf 'old\n' > $$d/fidocad/SKILL.md && \
	  printf 'stale\n' > $$d/fidocad/stale.txt && \
	  ./build/fcai skill install $$d >/dev/null && \
	  test -L $$d/fidocad && test -f $$d/fidocad/SKILL.md && test ! -e $$d/fidocad/stale.txt; \
	  rc=$$?; rm -rf $$d; exit $$rc
	$(if $(and $(HAVE_QT),$(HAVE_QT_TEST)),$(MAKE) test-gui)

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O1 -g -fsanitize=address,undefined" \
		LDFLAGS="-fsanitize=address,undefined" test

# The agent skill ships in DATADIR and is copied into each agent harness found in the
# user's home (scripts/install-skill.sh). Staged installs (DESTDIR) never touch $HOME.
install: all
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(DATADIR)/lib $(DESTDIR)$(DATADIR)/fonts $(DESTDIR)$(DATADIR)/skills
	install -m 755 build/fcai $(DESTDIR)$(BINDIR)/fcai
	if [ -f build/fcai-gui ]; then install -m 755 build/fcai-gui $(DESTDIR)$(BINDIR)/fcai-gui; fi
	install -m 644 share/lib/*.fcl $(DESTDIR)$(DATADIR)/lib/
	install -m 644 share/fonts/*.ttf $(DESTDIR)$(DATADIR)/fonts/
	rm -rf $(DESTDIR)$(DATADIR)/skills/fidocad
	cp -R share/skills/fidocad $(DESTDIR)$(DATADIR)/skills/
ifeq ($(DESTDIR),)
	sh scripts/install-skill.sh share/skills/fidocad
endif

install-skill:
	sh scripts/install-skill.sh share/skills/fidocad

# System-wide install under the project's install script (Linux and macOS).
system-install:
	scripts/install.sh install

# Refresh the system-wide copy after a source change.
system-update:
	scripts/install.sh update

clean:
	rm -rf build
