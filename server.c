#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <ctype.h>
#include <sys/stat.h>

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
	char buffer[MAX_REQUEST_SIZE];
	char path_buffer[MAX_REQUEST_SIZE * 2];
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
			perror("accept");
			exit(EXIT_FAILURE);
		}
		
		// Start a pleasant conversation.
		n = read(connfd, buffer, MAX_REQUEST_SIZE);
		if (n < 0) {
			perror("read");
			exit(EXIT_FAILURE);
		}
		
		// An awfully direct way of checking that this is a GET request.
		int valid_request = 0;
		if (tolower(buffer[0]) 		== 	'g' 
			  && tolower(buffer[1]) == 	'e' 
			  && tolower(buffer[2]) == 	't' 
			  && buffer[3] 			== 	' '
			  && buffer[4] 			== 	'/') {
			valid_request = 1;
		}
		
		// Get the length of the requested path.
		int len = 0;
		for (int i = 4; i < n; i++) {
			// Check if we've reached the end of the path.
			if (buffer[i] == ' ') {
				break;
			}
			
			// Look for any invalid path components.
			if (len > 2
				  && buffer[i] 	   == '/'
				  && buffer[i - 1] == '.'
				  && buffer[i - 2] == '.') {
				valid_request = 0;
				break;
			}
			
			len++;
		}
		
		// Time to prepare a response.
		if (!valid_request) {
			printf("Bad syntax.\n");
			snprintf(buffer, sizeof(buffer), "HTTP/1.0 400 Bad Request\r\n");
		} else {
			// Concatenate the root directory and requested path.
			strcpy(path_buffer, argv[PATH_ARG]);
			strncat(path_buffer, buffer + 4, len);
			printf("Requested: '%s'\n", path_buffer);
			
			FILE *fp = fopen(path_buffer, "rb");
			if (fp != NULL) {
				printf("Found!\n");
				
				struct stat stat_buffer;
				if (fstat(fileno(fp), &stat_buffer) < 0) {
					perror("fstat");
					exit(EXIT_FAILURE);
				}
				
				
				snprintf(buffer, sizeof(buffer), "HTTP/1.0 200 OK\r\nContent-Length: %ld\r\n\r\n", stat_buffer.st_size);
				fclose(fp);
			} else {
				printf("Not found.\n");
				snprintf(buffer, sizeof(buffer), "HTTP/1.0 404 Not Found\r\n");
			}
		}		
		
		// Send our message to the client.
		n = write(connfd, buffer, strlen(buffer));
		if (n < 0) {
			perror("write");
			exit(EXIT_FAILURE);
		}		
		
		// Close the connection.
		close(connfd);
	}

	return 0;
}
