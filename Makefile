CC      = cc
CFLAGS  = -Wall -Wextra -std=c11 -g -I.

.PHONY: all run clean

all: examples/example

examples/example: examples/example.c csv.h
	$(CC) $(CFLAGS) -o examples/example examples/example.c

run: examples/example
	./examples/example

clean:
	rm -f examples/example
