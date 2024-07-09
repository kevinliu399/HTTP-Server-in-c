#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>

int main() {
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

    printf("Waiting for a client to connect...\n");
    client_addr_len = sizeof(client_addr);

    client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
    if (client_fd < 0) {
        printf("Accept failed: %s\n", strerror(errno));
        return 1;
    }
    printf("Client connected\n");

    char buffer[4096];
    ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0); // This is the request from the client
    if (bytes_received < 0) {
        printf("Receive failed: %s\n", strerror(errno));
        return 1;
    }
    buffer[bytes_received] = '\0';

    char* token;
    char* path = NULL;
    char* method = NULL;
    char* pathForEcho = NULL;
    char* echoContent = NULL;

    token = strtok(buffer, "\r\n");
    if (token) {
        method = strtok(token, " ");
        path = strtok(NULL, " ");
    }

    if (path) {
        char* pathForEcho = strtok(path, "/");
        if (pathForEcho) {
            echoContent = strtok(NULL, "/");
        }
    }

    if (path && strcmp(path, "/") == 0) {
        char* response = "HTTP/1.1 200 OK\r\n\r\n";
        send(client_fd, response, strlen(response), 0);
    } else if (path && strcmp(path, "/echo") == 0) {
        char bufferForEchoContent[4096];
        snprintf(bufferForEchoContent, sizeof(bufferForEchoContent), "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\n\r\n%s", strlen(echoContent), echoContent);
        send(client_fd, bufferForEchoContent, strlen(bufferForEchoContent), 0);
    } else {
        char* response = "HTTP/1.1 404 Not Found\r\n\r\n";
        send(client_fd, response, strlen(response), 0);
    }

    close(client_fd);
    close(server_fd);

    return 0;
}