CC=gcc
CFLAGS=-Wall

server: server.c
	$(CC) $(CFLAGS) server.c -o server

clean:
	rm -f server
