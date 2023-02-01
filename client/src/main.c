#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_PORT 5000
#define BUF_SIZE 256

int main(int argc, char *argv[])
{
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUF_SIZE];

    if (argc < 2)
    {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return EXIT_FAILURE;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        return EXIT_FAILURE;
    }

    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0)
    {
        perror("connect");
        return EXIT_FAILURE;
    }

    printf("Connected to server.\n");

    while(fgets(buffer, BUF_SIZE, stdin) != NULL)
    {
        ssize_t n = send(sockfd, buffer, strlen(buffer), 0);

        if (n < 0)
        {
            perror("send");
            return EXIT_FAILURE;
        }

        printf("Written to server\n");
        write(STDOUT_FILENO, buffer, n);

        ssize_t m = recv(sockfd, buffer, BUF_SIZE, 0);

        if (m < 0)
        {
            perror("recv");
            return EXIT_FAILURE;
        }

        buffer[m] = '\0';
        printf("Server: %s", buffer);
    }

    close(sockfd);

    return EXIT_SUCCESS;
}
