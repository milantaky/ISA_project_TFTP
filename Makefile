CC = gcc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Werror

all: tftp-server tftp-client

tftp-server: tftp-server.c
	$(CC) $(CFLAGS) tftp-server.c -o tftp-server

tftp-client: tftp-client.c
	$(CC) $(CFLAGS) tftp-client.c -o tftp-client

clean:
	rm -f tftp-server tftp-client
