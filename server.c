/*

server.c

A basic web server that handles HTTP 1.0 GET requests and responds appropiately.

*/

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

#define N_THREADS       10
#define MAX_SYN_PACKETS 10

#define LONGEST_MIME_TYPE   25   // The longest MIME type we will put in the header.
#define INITIAL_PATH_SIZE   100  // The initial size of the path buffer.
#define INITIAL_OUTPUT_SIZE 200  // The initial size of the outgoing buffer.
#define MAX_REQUEST_SIZE    2000 // The largest GET request we can expect.

#define PROTOCOL_ARG 1 // The argv index for IPv4/IPv6.
#define PORT_ARG     2 // The argv index for the port to use.
#define PATH_ARG     3 // The argv index for the web root path.

// A structure for storing parameters to pass to threads.
typedef struct thread_info {
	int connfd;
	char *web_root;
} thread_info_t;

// Callable function for threads to communicate on a connection socket.
void *handle_connection(void *p) {

	// Tidy up the parameters passed.
	thread_info_t *t_info = (thread_info_t *) p;
	int n, connfd = t_info->connfd;
	char *web_root = t_info->web_root;

	printf("Started thread handling socket %d\n", connfd);

	// Prepare buffers.
	char i_buffer[MAX_REQUEST_SIZE + 1];

	size_t o_buffer_n = INITIAL_OUTPUT_SIZE;
	char *o_buffer = malloc(INITIAL_OUTPUT_SIZE * sizeof(*o_buffer));
	if (o_buffer == NULL) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	// Start a pleasant conversation.
	memset(i_buffer, 0, sizeof(i_buffer));
	int bytes_read = 0;
	while (1) {
		// Read into the buffer, shifted over accordingly.
		n = recv(connfd, i_buffer + bytes_read, MAX_REQUEST_SIZE - bytes_read, 0);
		if (n < 0) {
			perror("recv");
			exit(EXIT_FAILURE);
		}
		bytes_read += n;

		// If there's two new-lines in a row, or the client disconnected, the HTTP message is complete.
		if (n == 0 || strstr(i_buffer, "\n\n\n\n") != NULL || strstr(i_buffer, "\r\n\r\n") != NULL) {
			break;
		}
	}

	if (n == 0) {
		printf("Client disconnected unexpectedly.\n");
	} else {
		printf("Bytes read: %d\n", bytes_read);

		// Time to prepare a response.
		// An awfully direct way of checking that this is a GET request;
		// if it starts with the 5 characters "GET /" (with any casing), it's valid!
		if (!(bytes_read > 4
			  && tolower(i_buffer[0]) == 'g'
			  && tolower(i_buffer[1]) == 'e'
			  && tolower(i_buffer[2]) == 't'
			  && i_buffer[3]          == ' '
			  && i_buffer[4]          == '/')) {
			printf("Bad syntax. Returning 400.\n");
			snprintf(o_buffer, o_buffer_n, "HTTP/1.0 400 Bad Request\r\n\r\n");
			o_buffer_n = strlen(o_buffer);
		} else {

			// Get the requested path and copy it.
			char request_path_buffer[MAX_REQUEST_SIZE];
			for (int i = 4; i < bytes_read; i++) {
				// Check if we've reached the end of the path.
				if (i_buffer[i] == ' ') {
					// Null terminate and exit loop.
					request_path_buffer[i - 4] = 0;
					break;
				}
				request_path_buffer[i - 4] = i_buffer[i];
			}

			// Concatenate the canonical root with the requested path.
			char *full_path = malloc((strlen(web_root) + MAX_REQUEST_SIZE + 1) * sizeof(*full_path));
			if (full_path == NULL) {
				perror("malloc");
				exit(EXIT_FAILURE);
			}

			strcpy(full_path, web_root);
			strcat(full_path, request_path_buffer);

			printf("Requested: '%s'\n", full_path);

			// Check the path, ensuring it has no escape components,
			// that it exists, and that it is a normal readable file.
			struct stat stat_buffer;
			if (strstr(request_path_buffer, "/../") == NULL
				  && stat(full_path, &stat_buffer) == 0
				  && S_ISREG(stat_buffer.st_mode)) {

				// Open the requested file.
				FILE *fp = fopen(full_path, "rb");
				if (fp == NULL) {
					perror("fopen");
					exit(EXIT_FAILURE);
				}

				// Look for the last dot in the filename.
				int ext_start = strlen(full_path);
				for (int i = ext_start; i > 0; i--) {
					if (full_path[i] == '.') {
						ext_start = i;
						break;
					}
				}

				// Get the corresponding MIME type.
				char mime_type[LONGEST_MIME_TYPE];
				if (strcmp(full_path + ext_start, ".html")       == 0) {
					strcpy(mime_type, "text/html");
				} else if (strcmp(full_path + ext_start, ".jpg") == 0) {
					strcpy(mime_type, "image/jpeg");
				} else if (strcmp(full_path + ext_start, ".css") == 0) {
					strcpy(mime_type, "text/css");
				} else if (strcmp(full_path + ext_start, ".js")  == 0) {
					strcpy(mime_type, "text/javascript");
				} else {
					strcpy(mime_type, "application/octet-stream");
				}

				// Construct the header.
				printf("Found! Returning 200.\n");
				snprintf(o_buffer, o_buffer_n,
					  "HTTP/1.0 200 OK\r\nContent-Length: %ld\r\nContent-Type: %s\r\n\r\n",
					  stat_buffer.st_size, mime_type);

				// "Append" the file contents.
				int header_length = strlen(o_buffer);
				o_buffer_n = header_length + stat_buffer.st_size;
				o_buffer = realloc(o_buffer, o_buffer_n * sizeof(*o_buffer));
				if (o_buffer == NULL) {
					perror("realloc");
					exit(EXIT_FAILURE);
				}

				fread(o_buffer + header_length, o_buffer_n, 1, fp);

				fclose(fp);
			} else {
				printf("Not found. Returning 404.\n");
				snprintf(o_buffer, o_buffer_n, "HTTP/1.0 404 Not Found\r\n\r\n");
				o_buffer_n = strlen(o_buffer);
			}

			free(full_path);
		}

		// Send our message to the client.
		int bytes_sent = 0;
		while (bytes_sent < o_buffer_n) {
			n = send(connfd, o_buffer + bytes_sent, o_buffer_n - bytes_sent, 0);
			if (n < 0) {
				perror("send");
				exit(EXIT_FAILURE);
			}
			bytes_sent += n;
		}
	}

	// Close the connection and free memory.
	free(o_buffer);
	free(t_info);
	close(connfd);
	return NULL;
}

/* Basic TCP socket creation and connection code adapted from
various COMP30023 materials - namely, server.c from Practical 9
and Week 8 lecture notes. Credit to Ayesha Ahmed and Lachlan Andrew. */

int main(int argc, char **argv) {

	// Very basic input checking.
	if (argc != 4) {
		fprintf(stderr, "Invalid number of arguments!\n");
		exit(EXIT_FAILURE);
	}

	// Get the canonical file path of the web root.
	char *real_root = realpath(argv[PATH_ARG], NULL);
	if (real_root == NULL) {
		perror("realpath");
		exit(EXIT_FAILURE);
	}

	// Initialize variables.
	pthread_t tid[N_THREADS];
	thread_info_t *t_info[N_THREADS];
	int thread_n = 0;

	int listenfd = 0, re = 1, s;
	struct addrinfo hints, *res, *rp;

	// Determine whether to use IPv4 or IPv6.
	int is_ipv4 = 1;
	if (strcmp(argv[PROTOCOL_ARG], "6") == 0) {
		is_ipv4 = 0;
	}

	// Set up our connection.
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
			if (listenfd < 0) {
				perror("socket");
				exit(EXIT_FAILURE);
			} else {
				break;
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
		int connfd = accept(listenfd, NULL, NULL);
		if (connfd < 0) {
			perror("accept");
			exit(EXIT_FAILURE);
		}

		// Initialize parameters to pass to a new thread.
		t_info[thread_n] = malloc(sizeof(t_info));
		if (t_info[thread_n] == NULL) {
			perror("malloc");
			exit(EXIT_FAILURE);
		}

		t_info[thread_n]->connfd = connfd;
		t_info[thread_n]->web_root = real_root;

		// Create a new detached thread to handle this connection.
		if (pthread_create(&tid[thread_n], NULL, handle_connection, t_info[thread_n])) {
			perror("pthread_create");
			exit(EXIT_FAILURE);
		}

		if (pthread_detach(tid[thread_n])) {
			perror("pthread_detach");
			exit(EXIT_FAILURE);
		}

		// Cycle through the threads. This will probably have cataclysmic
		// consequences if more than N_THREADS threads run at a time, but maybe
		// not since each thread is detached anyway. I don't know.
		// Multi-threading is hard :(
		thread_n++;
		if (thread_n >= N_THREADS) {
			thread_n = 0;
		}
	}

	free(real_root);
	close(listenfd);
	return 0;
}
