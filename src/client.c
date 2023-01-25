#include "client.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_PORT 4981
#define BUF_SIZE 256

int run_client(struct options *opts) {
    int socket_fd;
    struct sockaddr_in server_addr;
    char buffer[BUF_SIZE];


    socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_fd < 0)
    {
        fprintf(stderr, "Failed to create socket"); //NOLINT(cert-err33-c)
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (opts->ip_address == NULL) {
        fprintf(stderr, "Please enter valid IP address"); //NOLINT(cert-err33-c)
        return EXIT_FAILURE;
    }

    if (inet_pton(AF_INET, opts->ip_address, &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        return EXIT_FAILURE;
    }

    if (connect(socket_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0)
    {
        fprintf(stderr, "Connection failed, verify IP or Server Status"); //NOLINT(cert-err33-c)
        return EXIT_FAILURE;
    }

    printf("Connected to server.\n");

    while(fgets(buffer, BUF_SIZE, stdin) != NULL)
    {
        ssize_t n = send(socket_fd, buffer, strlen(buffer), 0);

        if (n < 0)
        {
            perror("send");
            return EXIT_FAILURE;
        }

        printf("Written to server\n");
        write(STDOUT_FILENO, buffer, n);

        ssize_t m = recv(socket_fd, buffer, BUF_SIZE, 0);

        if (m < 0)
        {
            perror("recv");
            return EXIT_FAILURE;
        }

        buffer[m] = '\0';
        printf("Server: %s", buffer);
    }

    close(socket_fd);

    return EXIT_SUCCESS;
}
