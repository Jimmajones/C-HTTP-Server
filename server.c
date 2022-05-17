#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <ctype.h>
#include <sys/stat.h>

#define IMPLEMENTS_IPV6
#define MULTITHREADED

#define LONGEST_MIME_TYPE 25 // The longest MIME type we will put in the header.
#define INITIAL_OUTPUT_SIZE 500 // The initial size of the outgoing buffer.
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

	struct addrinfo hints, *res, *rp;
	
	int is_ipv4 = 1;
	if (strcmp(argv[PROTOCOL_ARG], "6") == 0) {
		is_ipv4 = 0;
	}

	// Set up our connection - IPv4, TCP, and passive.
	memset(&hints, 0, sizeof(hints));
	if (is_ipv4) {
		hints.ai_family = AF_INET;
	} else {
		hints.ai_family = AF_INET6;
	}
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	
	// Initialize the Internet address(es) we will bind to.
	s = getaddrinfo(NULL, argv[PORT_ARG], &hints, &res);
	if (s != 0) {
		perror("getaddrinfo");
		exit(EXIT_FAILURE);
	}
	
	// Create the socket on the specified address.
	for (rp = res; rp != NULL; rp = rp->ai_next) {
		if (rp->ai_family == hints.ai_family) {
			listenfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
			if (listenfd > 0) {
				break;
			} else {
				perror("socket");
				exit(EXIT_FAILURE);
			}
		}
	}
	
	// Re-use the same address/port repeatedly.
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &re, sizeof(re)) < 0) {
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}
	
	// Bind the specified socket to the specified address.
	if (bind(listenfd, rp->ai_addr, rp->ai_addrlen) < 0) {
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
		
		char i_buffer[MAX_REQUEST_SIZE];
		char path_buffer[MAX_REQUEST_SIZE * 2];
		int o_buffer_n = INITIAL_OUTPUT_SIZE;
		char *o_buffer = malloc(INITIAL_OUTPUT_SIZE * sizeof(*o_buffer));

		// Start a pleasant conversation.
		n = read(connfd, i_buffer, MAX_REQUEST_SIZE);
		if (n < 0) {
			perror("read");
			exit(EXIT_FAILURE);
		}
		
		// An awfully direct way of checking that this is a GET request.
		int valid_request = 0;
		if (tolower(i_buffer[0]) 		== 	'g' 
			  && tolower(i_buffer[1]) == 	'e' 
			  && tolower(i_buffer[2]) == 	't' 
			  && i_buffer[3] 			== 	' '
			  && i_buffer[4] 			== 	'/') {
			valid_request = 1;
		}

		// Time to prepare a response.
		if (valid_request < 0) {
			printf("Bad syntax.\n");
			snprintf(o_buffer, o_buffer_n, "HTTP/1.0 400 Bad Request\r\n");
			o_buffer_n = strlen(o_buffer);
		} else {
			// Get the length of the requested path.
			int len = 0;
			for (int i = 4; i < n; i++) {
				// Check if we've reached the end of the path.
				if (i_buffer[i] == ' ') {
					break;
				}
				
				// Look for any invalid path components.
				if (len > 2
					  && i_buffer[i] 	 == '/'
					  && i_buffer[i - 1] == '.'
					  && i_buffer[i - 2] == '.') {
					valid_request = 0;
					break;
				}
				
				len++;
			}
			// Concatenate the root directory and requested path.
			strcpy(path_buffer, argv[PATH_ARG]);
			strncat(path_buffer, i_buffer + 4, len);
			printf("Requested: '%s'\n", path_buffer);
			
			// Check the path.
			struct stat stat_buffer;
			if (valid_request && stat(path_buffer, &stat_buffer) == 0 && S_ISREG(stat_buffer.st_mode)) {
				printf("Found!\n");
				// Open the requested file.
				FILE *fp = fopen(path_buffer, "rb");
				if (fp == NULL) {
					perror("fopen");
					exit(EXIT_FAILURE);
				}
				
				int ext_start = strlen(path_buffer);
				for (int i = ext_start; i > 0; i--) {
					if (path_buffer[i] == '.') {
						ext_start = i;
						break;
					}
				}
				
				// Get the corresponding MIME type.
				char mime_type[LONGEST_MIME_TYPE];
				if (strcmp(path_buffer + ext_start, ".html") == 0) {
					strcpy(mime_type, "text/html");
				} else if (strcmp(path_buffer + ext_start, ".jpg") == 0) {
					strcpy(mime_type, "image/jpeg");
				} else if (strcmp(path_buffer + ext_start, ".css") == 0) {
					strcpy(mime_type, "text/css");
				} else if (strcmp(path_buffer + ext_start, ".js") == 0) {
					strcpy(mime_type, "text/javascript");
				} else {
					strcpy(mime_type, "application/octet-stream");
				}
				
				// Construct the header.
				snprintf(o_buffer, o_buffer_n, 
					  "HTTP/1.0 200 OK\r\nContent-Length: %ld\r\nContent-Type: %s\r\n\r\n", 
					  stat_buffer.st_size, mime_type);
				int header_length = strlen(o_buffer);
				// "Append" the file contents.
				o_buffer_n = header_length + stat_buffer.st_size;
				o_buffer = realloc(o_buffer, o_buffer_n * sizeof(*o_buffer));
				fread(o_buffer + header_length, o_buffer_n, 1, fp);
				fclose(fp);				
			} else {
				printf("Not found.\n");
				snprintf(o_buffer, o_buffer_n, "HTTP/1.0 404 Not Found\r\n");
				o_buffer_n = strlen(o_buffer);
			}
		}		
		
		// Send our message to the client.
		n = write(connfd, o_buffer, o_buffer_n);
		if (n < 0) {
			perror("write");
			exit(EXIT_FAILURE);
		}		
		
		// Close the connection.
		free(o_buffer);
		close(connfd);
	}

	return 0;
}
