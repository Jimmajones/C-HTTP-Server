CC=gcc
CFLAGS=-Wall -pthread

server: server.c
	$(CC) $(CFLAGS) server.c -o server

clean:
	rm -f server
