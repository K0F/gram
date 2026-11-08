CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LDLIBS   = -lm

UNAME_O := $(shell uname -o 2>/dev/null || echo unknown)
ifneq ($(filter Android,$(UNAME_O))$(filter /data/data/com.termux/%,$(PREFIX)),)
  IS_TERMUX := 1
  PREFIX    ?= /data/data/com.termux/files/usr
  BINDIR    ?= $(PREFIX)/bin
  DATADIR   ?= $(PREFIX)/share/gram
else
  IS_TERMUX := 0
  PREFIX    ?= $(HOME)/.local
  BINDIR    ?= $(HOME)/bin
  DATADIR   ?= $(PREFIX)/share/gram
endif

HAVE_RAYLIB := $(shell pkg-config --exists raylib 2>/dev/null && echo 1 || ([ -f /usr/include/raylib.h ] && echo 1 || echo 0))

SRC = src/util.c src/omicron.c src/analysis.c src/render.c \
      src/library.c src/plan.c src/visual.c src/av_render.c \
      src/compose.c src/edit.c src/slides.c src/gomfont.c \
      src/flags.c src/title.c src/partition.c src/main.c
OBJ = $(SRC:.c=.o)
BIN = gram
DADA = dada/dada
GENMONTAGE = genmontage/genmontage
RAYMONTAGE = genmontage/raymontage
TJ = tj/tj
MICHACKA = michacka/michacka
OMICRON = OperatorOmikron/build/omicron
KOFSUM = OperatorOmikron/build/kofsum

SUBMODULE_BINS = $(DADA) $(GENMONTAGE) $(TJ) $(MICHACKA) $(OMICRON) $(KOFSUM)
ifeq ($(HAVE_RAYLIB),1)
  SUBMODULE_BINS += $(RAYMONTAGE)
endif

all: $(BIN) $(SUBMODULE_BINS)

submodules: $(SUBMODULE_BINS)

deps:
	./install_deps.sh

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ): src/util.h

$(DADA):
	@test -f dada/Makefile || git submodule update --init -- dada
	$(MAKE) -C dada CC="$(CC)"

$(GENMONTAGE):
	@test -f genmontage/Makefile || git submodule update --init -- genmontage
	$(MAKE) -C genmontage CC="$(CC)" genmontage

$(RAYMONTAGE):
	@test -f genmontage/Makefile || git submodule update --init -- genmontage
	$(MAKE) -C genmontage CC="$(CC)" raymontage

$(TJ):
	@test -f tj/Makefile || git submodule update --init -- tj
	$(MAKE) -C tj CC="$(CC)"

$(MICHACKA): $(TJ)
	@test -f michacka/Makefile || git submodule update --init -- michacka
	$(MAKE) -C michacka CC="$(CC)" michacka

$(OMICRON) $(KOFSUM):
	@test -f OperatorOmikron/Makefile || git submodule update --init -- OperatorOmikron
	$(MAKE) -C OperatorOmikron CC="$(CC)" build

install: all
	install -d "$(DESTDIR)$(BINDIR)"
	install -d "$(DESTDIR)$(DATADIR)"
	install -m 0755 $(BIN) "$(DESTDIR)$(BINDIR)/gram"
	install -m 0755 $(DADA) "$(DESTDIR)$(BINDIR)/dada"
	install -m 0755 $(GENMONTAGE) "$(DESTDIR)$(BINDIR)/genmontage"
	install -m 0755 $(TJ) "$(DESTDIR)$(BINDIR)/tj"
	install -m 0755 $(MICHACKA) "$(DESTDIR)$(BINDIR)/michacka"
	install -m 0755 $(OMICRON) "$(DESTDIR)$(BINDIR)/omicron"
	install -m 0755 $(KOFSUM) "$(DESTDIR)$(BINDIR)/kofsum"
	@if [ -x "$(RAYMONTAGE)" ]; then \
		install -m 0755 $(RAYMONTAGE) "$(DESTDIR)$(BINDIR)/raymontage"; \
	fi
	@if [ -f tools/gram-yt-music ]; then \
		install -m 0755 tools/gram-yt-music "$(DESTDIR)$(BINDIR)/gram-yt-music"; \
	fi
	@if [ -f ../postgen/main.go ] && command -v go >/dev/null 2>&1; then \
		(cd ../postgen && go build -o postgen main.go) && \
		install -m 0755 ../postgen/postgen "$(DESTDIR)$(BINDIR)/postgen"; \
	fi
	install -m 0644 dada/noise.png "$(DESTDIR)$(DATADIR)/noise.png"
	install -m 0644 genmontage/gomotor.ttf "$(DESTDIR)$(DATADIR)/gomotor.ttf"
	@echo "Installed binaries to $(DESTDIR)$(BINDIR) and assets to $(DESTDIR)$(DATADIR)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/gram" \
	      "$(DESTDIR)$(BINDIR)/dada" \
	      "$(DESTDIR)$(BINDIR)/genmontage" \
	      "$(DESTDIR)$(BINDIR)/raymontage" \
	      "$(DESTDIR)$(BINDIR)/tj" \
	      "$(DESTDIR)$(BINDIR)/michacka" \
	      "$(DESTDIR)$(BINDIR)/omicron" \
	      "$(DESTDIR)$(BINDIR)/kofsum" \
	      "$(DESTDIR)$(BINDIR)/gram-yt-music"
	rm -rf "$(DESTDIR)$(DATADIR)"

test: $(BIN)
	$(CC) $(CFLAGS) -Isrc -o test_gram tests/test_gram.c $(filter-out src/main.c,$(SRC)) $(LDLIBS)
	./test_gram

smoke: $(BIN)
	./$(BIN) omicron --reverse 42 | head -3
	./$(BIN) omicron -n 4 --limit 8

smoke-av: $(BIN)
	./$(BIN) compose day 777 --parts 1 --len 20 --out smoke_gram --max 12 --av

demo:
	rm -rf out
	./demo/run.sh

clean-render:
	rm -rf out

clean:
	rm -f $(OBJ) $(BIN) test_gram
	$(MAKE) -C dada clean 2>/dev/null || true
	$(MAKE) -C genmontage clean 2>/dev/null || true
	$(MAKE) -C tj clean 2>/dev/null || true
	$(MAKE) -C michacka clean 2>/dev/null || true
	$(MAKE) -C OperatorOmikron clean 2>/dev/null || true

.PHONY: all submodules deps install uninstall test smoke smoke-av demo clean-render clean
