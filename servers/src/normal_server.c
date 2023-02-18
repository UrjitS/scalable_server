#include "server.h"
#include "util.h"
#include <arpa/inet.h>
#include <dc_c/dc_stdio.h>
#include <dc_c/dc_string.h>
#include <dc_env/env.h>
#include <dc_error/error.h>
#include <dc_posix/dc_unistd.h>
#include <dc_posix/sys/dc_socket.h>
#include <dc_util/io.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define READ_BUFFER_SIZE 1024
#define CONVERT_TO_MS 1000
#define BACKLOG 5

static volatile sig_atomic_t running;   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static int setup_server(struct dc_env *env, struct dc_error *err, struct options *opts);
void handle_connection(struct dc_env *env, struct dc_error *error, int socket_fd);
static int handle_client_data(struct dc_env *env, struct dc_error *err, int clients);

int run_normal_server(struct dc_env * env, struct dc_error * error, struct options *opts) {
    // Trace this function
    DC_TRACE(env);

    int listen_fd;


    listen_fd = setup_server(env, error, opts);

    running = 1;

    while(running)
    {
        // Log the time each client took to be handled
        clock_t beginning_connection = clock();

        handle_connection(env, error, listen_fd);

        clock_t end = clock();
        double time_spent = ((double)(end - beginning_connection) / CLOCKS_PER_SEC) * CONVERT_TO_MS;
        write_to_file(opts, "Normal Server", "handle_connection", time_spent);
    }

    return 0;
}

static int setup_server(struct dc_env *env, struct dc_error *err, struct options *opts)
{
    DC_TRACE(env);

    int listener;
    int option_value;
    struct sockaddr_in server_addr;

    listener = dc_socket(env, err, AF_INET, SOCK_STREAM, 0);

    if (listener < 0)
    {
        dc_perror(env, "socket");
        return -1;
    }

    option_value = 1;
    dc_setsockopt(env, err, listener, SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof(option_value));

    dc_memset(env, &server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(opts->ip_address);
    server_addr.sin_port = htons(opts->port_out);

    if(dc_bind(env, err, listener, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0)
    {
        dc_perror(env, "bind");
        dc_close(env, err, listener);
        return -1;
    }

    if(dc_listen(env, err, listener, BACKLOG) < 0)
    {
        dc_perror(env, "listen");
        dc_close(env, err, listener);
        return -1;
    }

    return listener;
}

void handle_connection(struct dc_env *env, struct dc_error *error, int socket_fd)
{
    DC_TRACE(env);

    struct sockaddr_in client_addr;
    socklen_t client_len;
    int client_fd;

    printf("Setup server and awaiting connection\n");

    // Accept connection
    dc_memset(env, &client_addr, 0, sizeof(client_addr));
    client_len = sizeof(client_addr);
    client_fd = dc_accept(env, error, socket_fd, (struct sockaddr*) &client_addr, &client_len);
    if(client_fd < 0)
    {
        dc_perror(env, "accept");
        return;
    }

    // Get connection information
    printf("New connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));    // NOLINT(concurrency-mt-unsafe)
    int socket_error = 0;

    while (socket_error == 0) {
        if (recv(client_fd, NULL, 1, MSG_PEEK | MSG_DONTWAIT) == 0) {
            close(client_fd);
            return;
        }
        socket_error = handle_client_data(env, error, client_fd);
        if (socket_error != 0) {
            close(client_fd);
        }
    }
    close(client_fd);
}

static int handle_client_data(struct dc_env *env, struct dc_error *err, int clients) {
    DC_TRACE(env);

    if (recv(clients, NULL, 1, MSG_PEEK | MSG_DONTWAIT) == 0) {
        return -1;
    }
    ssize_t bytes_read;
    char buffer[READ_BUFFER_SIZE];
    bytes_read = dc_read(env, err, clients, buffer, (READ_BUFFER_SIZE-1));
    buffer[bytes_read] = '\0';

    if(bytes_read <= 0 && recv(clients, NULL, 1, MSG_PEEK | MSG_DONTWAIT) == 0)
    {
        printf("Client disconnected\n");
        close(clients);
        return -1;
    }

    printf("Read from client\n");
    dc_write(env, err, STDOUT_FILENO, buffer, bytes_read);

    printf("Writing to client\n");
    uint16_t write_number = ntohs(bytes_read);
    dc_write(env, err, clients, &write_number, sizeof(write_number));
    return 0;
}
