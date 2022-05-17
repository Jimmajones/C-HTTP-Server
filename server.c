#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

#define MAX_REQUEST_SIZE 2000 // The largest GET request we can expect.
#define MAX_SYN_PACKETS 10

#define PROTOCOL_ARG 1
#define PORT_ARG 2
#define PATH_ARG 3

/* Basic TCP socket creation and connection code adapted from
various COMP30023 materials - namely, server.c from Practical 9
and Week 8 lecture notes. Credit to Ayesha Ahmed and Lachlan Andrew. */

int main(int argc, char **argv) {
	
	if (argc != 4) {
		perror("Invalid number of arguments!");
		exit(EXIT_FAILURE);
	}
	
	int listenfd = 0, connfd = 0, re = 1, s, n;
	char buffer[MAX_REQUEST_SIZE + 1]; // Add one for null-termination.
	struct addrinfo hints, *res;
	
	// Set up our connection - IPv4, TCP, and passive.
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	
	// Initialize the Internet address(es) we will bind to.
	s = getaddrinfo(NULL, argv[PORT_ARG], &hints, &res);
	if (s != 0) {
		perror("getaddrinfo");
		exit(EXIT_FAILURE);
	}
	
	// Create the socket on the specified address.
	listenfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (listenfd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
	
	// Re-use the same address/port repeatedly.
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &re, sizeof(re)) < 0) {
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}
	
	// Bind the specified socket to the specified address.
	if (bind(listenfd, res->ai_addr, res->ai_addrlen) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}
	freeaddrinfo(res);
	
	// Begin listening.
	if (listen(listenfd, MAX_SYN_PACKETS) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}
	
	while (1) {
		// Wait for an incoming connection and capture the remote address.
		struct sockaddr_storage client_addr;
		socklen_t client_addr_size = sizeof(client_addr);
		connfd = accept(listenfd, (struct sockaddr *) &client_addr, &client_addr_size);
		if (connfd < 0) {
			perror("read");
			exit(EXIT_FAILURE);
		}
		
		// Have a pleasant conversation.
		n = read(connfd, buffer, MAX_REQUEST_SIZE);
		if (n < 0) {
			perror("read");
			exit(EXIT_FAILURE);
		}
		buffer[n] = '\0';
		
		// Very, VERY hacky way of getting the filepath: Assume we're
		// receiving a GET request and skip straight to the "token" after
		// "GET", count how many characters long the requested resource is,
		// then copy the argument line and concatenate it with the resource.
		int i;
		for (i = 4; i < n; i++) {
			if (buffer[i] == ' ') {
				break;
			}
		}
		
		char path_buffer[MAX_REQUEST_SIZE];
		strcpy(path_buffer, argv[PATH_ARG]);
		strncat(path_buffer, buffer + 4, i - 4);
		printf("Requested: %s\n", path_buffer);
		// Check if the file exists.
		if (access(path_buffer, F_OK) == 0) {
			printf("Found.\n");
			snprintf(buffer, sizeof(buffer), "HTTP/1.0 200 OK\r\n");
		} else {
			printf("Not found.\n");
			snprintf(buffer, sizeof(buffer), "HTTP/1.0 404 Not Found\r\n");
		}
		write(connfd, buffer, strlen(buffer));
		
		// Close the connection.
		close(connfd);
	}

	return 0;
}
