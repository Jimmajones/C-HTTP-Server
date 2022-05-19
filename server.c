#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/stat.h>

#define IMPLEMENTS_IPV6
#define MULTITHREADED

#define N_THREADS 100

#define GET_PATH_START 4 // The index of a request where the pathname begins.

#define LONGEST_MIME_TYPE 25 // The longest MIME type we will put in the header.
#define INITIAL_OUTPUT_SIZE 500 // The initial size of the outgoing buffer.
#define MAX_REQUEST_SIZE 2000 // The largest GET request we can expect.
#define MAX_SYN_PACKETS 10

#define PROTOCOL_ARG 1
#define PORT_ARG 2
#define PATH_ARG 3

typedef struct thread_info {
	int connfd;
	char web_root[MAX_REQUEST_SIZE];
} thread_info_t;

void *handle_connection(void *p) {

	thread_info_t *t_info = (thread_info_t *) p;
	int n, connfd = t_info->connfd;
	printf("Started thread handling socket %d\n", connfd);
	
	char i_buffer[MAX_REQUEST_SIZE + 1];
	char path_buffer[MAX_REQUEST_SIZE * 2];
	int o_buffer_n = INITIAL_OUTPUT_SIZE;
	char *o_buffer = malloc(INITIAL_OUTPUT_SIZE * sizeof(*o_buffer));

	// Start a pleasant conversation.
	memset(i_buffer, 0, sizeof(i_buffer));
	int is_finished = 0, bytes_read = 0;
	while (!is_finished) {
		// Read into the buffer, shifted over accordingly.
		n = recv(connfd, i_buffer + bytes_read, MAX_REQUEST_SIZE - bytes_read, 0);
		if (n < 0) {
			perror("recv");
			exit(EXIT_FAILURE);
		}
		bytes_read += n;
		
		// If there's two new-lines in a row, the HTTP message is complete.
		if (strstr(i_buffer, "\n\n\n\n") != NULL || strstr(i_buffer, "\r\n\r\n") != NULL) {
			is_finished = 1;
		}
	}
	printf("==MESSAGE==\n%s\n==END==\nBytes read: %d\n", i_buffer, bytes_read);
	
	// An awfully direct way of checking that this is a GET request;
	// if it starts with the 5 characters "GET /" (with any casing), it's valid!
	int valid_request = 0;
	if (bytes_read > 4
		  && tolower(i_buffer[0]) 	== 	'g' 
		  && tolower(i_buffer[1]) 	== 	'e' 
		  && tolower(i_buffer[2]) 	== 	't' 
		  && i_buffer[3] 			== 	' '
		  && i_buffer[4] 			== 	'/') {
		valid_request = 1;
	}

	// Time to prepare a response.
	if (!valid_request) {
		printf("Bad syntax. Returning 400.\n");
		snprintf(o_buffer, o_buffer_n, "HTTP/1.0 400 Bad Request\r\n\r\n");
		o_buffer_n = strlen(o_buffer);
	} else {
		// Get the length of the requested path.
		int len = 0;
		for (int i = 4; i < bytes_read; i++) {
			// Check if we've reached the end of the path.
			if (i_buffer[i] == ' ') {
				break;
			}
			
			// Look for any invalid path components while we're here.
			if (len > 1
				  && i_buffer[i] 	 == '/'
				  && i_buffer[i - 1] == '.'
				  && i_buffer[i - 2] == '.') {
				valid_request = 0;
				break;
			}
			
			len++;
		}
		// Concatenate the root directory and requested path.
		strcpy(path_buffer, t_info->web_root);
		strncat(path_buffer, i_buffer + 4, len);
		printf("Requested: '%s'\n", path_buffer);
		
		// Check the path.
		struct stat stat_buffer;
		if (valid_request && stat(path_buffer, &stat_buffer) == 0 && S_ISREG(stat_buffer.st_mode)) {
			printf("Found! Returning 200.\n");
			
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
			
			// "Append" the file contents.
			int header_length = strlen(o_buffer);
			o_buffer_n = header_length + stat_buffer.st_size;
			o_buffer = realloc(o_buffer, o_buffer_n * sizeof(*o_buffer));
			fread(o_buffer + header_length, o_buffer_n, 1, fp);
			fclose(fp);				
		} else {
			printf("Not found. Returning 404.\n");
			snprintf(o_buffer, o_buffer_n, "HTTP/1.0 404 Not Found\r\n\r\n");
			o_buffer_n = strlen(o_buffer);
		}
	}		
	
	// Send our message to the client.
	n = send(connfd, o_buffer, o_buffer_n, 0);
	if (n < 0) {
		perror("send");
		exit(EXIT_FAILURE);
	}		
	
	// Close the connection.
	free(o_buffer);
	free(t_info);
	close(connfd);
	return NULL;
}
/* Basic TCP socket creation and connection code adapted from
various COMP30023 materials - namely, server.c from Practical 9
and Week 8 lecture notes. Credit to Ayesha Ahmed and Lachlan Andrew. */

int main(int argc, char **argv) {
	
	if (argc != 4) {
		perror("Invalid number of arguments!");
		exit(EXIT_FAILURE);
	}
	
	pthread_t tid[N_THREADS];
	thread_info_t *t_info[N_THREADS];
	int thread_n = 0;
	
	int listenfd = 0, re = 1, s;

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
	
	struct sockaddr_storage client_addr;
	socklen_t client_addr_size = sizeof(client_addr);	
	
	while (1) {
		// Wait for an incoming connection and capture the remote address.
		int connfd = accept(listenfd, (struct sockaddr *) &client_addr, &client_addr_size);
		if (connfd < 0) {
			perror("accept");
			exit(EXIT_FAILURE);
		}
		
		// Create a new thread to handle incoming connection.
		t_info[thread_n] = malloc(sizeof(t_info));
		t_info[thread_n]->connfd = connfd;
		strcpy(t_info[thread_n]->web_root, argv[PATH_ARG]);
		
		if (pthread_create(&tid[thread_n], NULL, handle_connection, t_info[thread_n])) {
			perror("pthread_create");
			exit(EXIT_FAILURE);
		}

		if (pthread_detach(tid[thread_n])) {
			perror("pthread_detach");
			exit(EXIT_FAILURE);
		}
		thread_n++;
		
		// Cycle through the threads. This will probably have cataclysmic
		// consequences if more than N_THREADS threads run at a time, but I don't
		// know how to multi-thread properly. Tee-hee.
		if (thread_n >= N_THREADS) {
			thread_n = 0;
		}
	}

	close(listenfd);
	return 0;
}
