CC = gcc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Werror

all: server client

server: server.c
	$(CC) $(CFLAGS) server.c -o server

client: client.c
	$(CC) $(CFLAGS) client.c -o client

clean:
	rm -f server client
