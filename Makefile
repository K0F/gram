CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LDLIBS   = -lm

SRC = src/util.c src/omicron.c src/analysis.c src/render.c \
      src/library.c src/plan.c src/visual.c src/av_render.c \
      src/compose.c src/edit.c src/main.c
OBJ = $(SRC:.c=.o)
BIN = gram

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ): src/util.h

test: $(BIN)
	$(CC) $(CFLAGS) -Isrc -o test_gram tests/test_gram.c $(filter-out src/main.c,$(SRC)) $(LDLIBS)
	./test_gram

smoke: $(BIN)
	./$(BIN) omicron --reverse 42 | head -3
	./$(BIN) omicron -n 4 --limit 8

smoke-av: $(BIN)
	./$(BIN) compose day 777 --parts 1 --len 20 --out smoke_gram --max 12 --av

clean:
	rm -f $(OBJ) $(BIN) test_gram

.PHONY: all test smoke smoke-av clean
