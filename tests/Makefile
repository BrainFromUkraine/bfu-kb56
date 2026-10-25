# SPDX-FileCopyrightText: 2026 BFU Electronics LTD
# SPDX-License-Identifier: Apache-2.0
CC      ?= cc
CFLAGS  := -std=c11 -O1 -g -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion \
           -fsanitize=address,undefined -fno-omit-frame-pointer
SRC     := test_app.c ../src/app/app.c ../src/app/keymap.c ../src/app/fifo.c ../src/app/utf8.c

all: run

test_app: $(SRC) $(wildcard ../src/app/*.h)
	$(CC) $(CFLAGS) -o $@ $(SRC)

run: test_app
	./test_app

clean:
	rm -f test_app

.PHONY: all run clean
