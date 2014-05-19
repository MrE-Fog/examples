CC=gcc
CFLAGS=-Wall
OBJ = server-tcp.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: server server-psk

server: server-tcp.c
	$(CC) -Wall -o server server-tcp.c

server-psk: server-psk.c
	$(CC) -Wall -o server-psk server-psk.c -lm -lcyassl

.PHONY: clean

clean:
	rm -f *.o server server-psk
