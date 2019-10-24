CC = gcc
CFLAGS = -pthread -Wall -pedantic -std=gnu99 -g
DEPS = 2310depot.h

all: 2310depot

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

2310depot: 2310depot.o
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: clean
clean:
	$(RM) 2310depot 2310depot.o