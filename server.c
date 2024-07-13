#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <pthread.h>
#include <zlib.h>

#define MAX_THREADS 10

void *handle_connection(void* arg);
char* g_directory;


int main(int argc, char *argv[]) {
	g_directory = "/tmp";
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--directory") == 0 && i + 1 < argc) {
			g_directory = argv[i + 1];
			break;
		}
	}

	// Disable output buffering
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	// You can use print statements as follows for debugging, they'll be visible when running tests.
	printf("Logs from your program will appear here!\n");

	int server_fd, client_fd, client_addr_len;
	struct sockaddr_in client_addr;

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Socket creation failed: %s\n", strerror(errno));
		return 1;
	}

	// Setting SO_REUSEADDR to avoid 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEADDR failed: %s\n", strerror(errno));
		return 1;
	}

	struct sockaddr_in serv_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(4221),
		.sin_addr = { htonl(INADDR_ANY) },
	};

	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("Bind failed: %s\n", strerror(errno));
		return 1;
	}

	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
		printf("Listen failed: %s\n", strerror(errno));
		return 1;
	}

	while (1) {
		printf("Waiting for a client to connect...\n");
		client_addr_len = sizeof(client_addr);

		client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
		if (client_fd < 0) {
			printf("Accept failed: %s\n", strerror(errno));
			continue; 
		}
		printf("Client connected\n");

		int *client_fd_ptr = malloc(sizeof(int));
		if (client_fd_ptr == NULL) {
			perror("Failed to allocate memory");
			close(client_fd);
			continue;
		}
		*client_fd_ptr = client_fd;


		// Create a new thread to handle the connection
		// pthread_t : thread identifier
		pthread_t thread;

		// pthread_create : create a new thread and run the handle_connection function
		if (pthread_create(&thread, NULL, handle_connection, client_fd_ptr) != 0) {
			perror("Failed to create thread");
			free(client_fd_ptr); // Free the memory allocated for the client_fd_ptr
			close(client_fd);
			continue;
		}
		

		// pthread_detach : detach the thread from the main thread
		// The thread will be automatically cleaned up when it finishes
		pthread_detach(thread);
	}

	close(client_fd);
	close(server_fd);

	return 0;
}

int file_exists(char* path) {
	FILE* file = fopen(path, "r");
	if (file) {
		fclose(file);
		return 1;
	}
	return 0;
}

void *handle_connection(void *arg) {
	int client_fd = *(int*)arg;
	free(arg); 

	char buffer[4096];
	ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0); // This is the request from the client
	if (bytes_received < 0) {
		printf("Receive failed: %s\n", strerror(errno));
		return NULL;
	}
	buffer[bytes_received] = '\0';

	char* pathForEcho = NULL;
	char* echoContent = NULL;
	char* saveptr;
	char* line = strtok_r(buffer, "\r\n", &saveptr); // Use strtok_r to parse the request line by line and save the state in saveptr
	char* method = NULL;
	char* path = NULL;

	if (line) {
		method = strtok(line, " ");
		path = strtok(NULL, " ");
		printf("Method: %s\n", method);
		printf("Path: %s\n", path);
	}

	// Check if the method is GET
	if (strcmp(method, "GET") == 0) {
		if (strncmp(path, "/files", 6) == 0) {
			char* filename = path + 6;  // Skip "/files/"
			char filepath[4096];
			snprintf(filepath, sizeof(filepath), "%s%s", g_directory, filename + 1);
			
			// printf("File request\n");
			// printf("File path: %s\n", filepath);

			FILE* file = fopen(filepath, "rb");
			if (file) {
				// move the pointer around to get the size of the file
				fseek(file, 0, SEEK_END);
				long file_size = ftell(file);
				fseek(file, 0, SEEK_SET);

				char* file_content = malloc(file_size);
				if (file_content == NULL) {
					fclose(file);
					close(client_fd);
					return NULL;
				}

				fread(file_content, 1, file_size, file);
				fclose(file);

				char response_header[4096];
				int header_length = snprintf(response_header, sizeof(response_header), 
					"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %ld\r\n\r\n",
					file_size);

				send(client_fd, response_header, header_length, 0);
				send(client_fd, file_content, file_size, 0);				

				free(file_content);
			} else {
				printf("File not found\n");
				char* response = "HTTP/1.1 404 Not Found\r\n\r\n";
				send(client_fd, response, strlen(response), 0);
			}

		} else if (strcmp(path, "/") == 0) {
			char* response = "HTTP/1.1 200 OK\r\n\r\n";
			send(client_fd, response, strlen(response), 0);
		} else if (strncmp(path, "/echo/", 6) == 0) {
			char* echoContent = path + 6;  // Skip "/echo/"
			int gzip_supported = 0;

			// Check for gzip support in Accept-Encoding header
			char* line;
			while ((line = strtok_r(NULL, "\r\n", &saveptr)) != NULL) {
				if (strncmp(line, "Accept-Encoding:", 16) == 0) {
					if (strstr(line, "gzip") != NULL) {
						gzip_supported = 1;
						break;
					}
				}
			}

			if (gzip_supported) {
				z_stream zs;

				// Set all fields to 0
				memset(&zs, 0, sizeof(zs));
				// deflateInit2: Initialize the zlib stream for compression		
				if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
					char* response = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
					send(client_fd, response, strlen(response), 0);
					close(client_fd);
					return NULL;
				}

				// Compress the content
				unsigned char out[4096];
				unsigned char* compressed = NULL;
				int total_compressed_size = 0;
				zs.next_in = (Bytef*)echoContent; // Assign the input buffer to the echoContent
				zs.avail_in = strlen(echoContent);

				do {
					zs.next_out = out;
					zs.avail_out = sizeof(out);

					deflate(&zs, Z_FINISH); // deflate: Compress the input buffer to the output buffer

					int have = sizeof(out) - zs.avail_out;
					compressed = realloc(compressed, total_compressed_size + have); // Reallocate memory for the compressed buffer
					memcpy(compressed + total_compressed_size, out, have);
					total_compressed_size += have;
				} while (zs.avail_out == 0);

				deflateEnd(&zs); // deflateEnd: Deallocate the memory used by the zlib stream

				char bufferResponse[4096];
				int response_length = snprintf(bufferResponse, sizeof(bufferResponse), 
					"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Encoding: gzip\r\nContent-Length: %d\r\n\r\n",
					total_compressed_size);
				send(client_fd, bufferResponse, response_length, 0);
				send(client_fd, compressed, total_compressed_size, 0);

				free(compressed);

			} else {
				// Send the response without compression

				char bufferResponse[4096];
				int response_length = snprintf(bufferResponse, sizeof(bufferResponse), 
					"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\n\r\n%s",
					strlen(echoContent), echoContent);
				send(client_fd, bufferResponse, response_length, 0);
			}
		} else if (strcmp(path, "/user-agent") == 0) {
			char* userAgent = NULL;

			while ((line = strtok_r(NULL, "\r\n", &saveptr)) != NULL) {
				if (strncmp(line, "User-Agent:", 11) == 0) {
					userAgent = line + 11;
					while (*userAgent && isspace(*userAgent)) {
						userAgent++;
					}
					break;
				}
			}

			if (userAgent) {
				char response[4096];
				int response_length = snprintf(response, sizeof(response), 
					"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\n\r\n%s",
					strlen(userAgent), userAgent);
				send(client_fd, response, response_length, 0);
			} else {
				char *response = "HTTP/1.1 404 Not Found\r\n\r\nUser-Agent header not found";
				send(client_fd, response, strlen(response), 0);
			}
			
		} else {
			char* response = "HTTP/1.1 404 Not Found\r\n\r\n";
			send(client_fd, response, strlen(response), 0);
		}
		
	} else if (strcmp(method, "POST") == 0) {
		if (strncmp(path, "/files/", 7) == 0) {
			printf("This is a POST request\n");
			char* filename = path + 7;
			char filepath[4096];
			snprintf(filepath, sizeof(filepath), "%s/%s", g_directory, filename);
			
			char* contentType = NULL;
			char* body = NULL;
			while ((line = strtok_r(NULL, "\r\n", &saveptr)) != NULL) {
				if (strncmp(line, "Content-Type:", 13) == 0) {
					printf("Line: %s\n", line); // Content-Length: 5
					contentType = line + 13;

					while (*contentType && isspace(*contentType)) {
						contentType++;
					}
				}
			}
			
			body = contentType + 24 + 4; // Skip "application/octet-stream\r\n\r\n"
			printf("Body: %s\n", body);


			FILE* file = fopen(filepath, "wb");
			if (file) {
				fwrite(body, 1, strlen(body), file);
				fclose(file);
				
				char* response = "HTTP/1.1 201 Created\r\n\r\n";
				send(client_fd, response, strlen(response), 0);
			} else {
				printf("Failed to open file for writing\n");
				char* response = "HTTP/1.1 404 Not Found\r\n\r\n";
				send(client_fd, response, strlen(response), 0);
			}
		} else {
			char* response = "HTTP/1.1 404 Not Found\r\n\r\n";
			send(client_fd, response, strlen(response), 0);
		}
	}
	close(client_fd);
	return NULL;
}