#include "server.h"
#include <arpa/inet.h>
#include <dc_c/dc_signal.h>
#include <dc_c/dc_stdio.h>
#include <dc_c/dc_stdlib.h>
#include <dc_c/dc_string.h>
#include <dc_env/env.h>
#include <dc_error/error.h>
#include <dc_posix/dc_unistd.h>
#include <dc_posix/sys/dc_select.h>
#include <dc_posix/sys/dc_socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>

#define CONVERT_TO_MS 1000
#define MAX_PENDING 5
#define MAX_CLIENTS 10
#define BUF_SIZE 256

static void ctrl_c_handler(int signum);
static int setup_server(struct dc_env *env, struct dc_error *err, struct options *opts);
static int run_server(struct dc_env *env, struct dc_error *err, int listener, int *clients, fd_set *read_fds, int *max_fd,struct options *opts);
static int wait_for_data(struct dc_env *env, struct dc_error *err, int listener, const int *clients, fd_set *read_fds, const int *max_fd);
static void handle_new_connections(struct dc_env *env, struct dc_error *err, int listener, int *clients, fd_set *read_fds, int *max_fd, struct options *opts);
static void handle_client_data(struct dc_env *env, struct dc_error *err, int *clients, fd_set* read_fds, struct options *opts);


static volatile sig_atomic_t done = false;   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

int run_select_server(struct dc_env * env, struct dc_error * error, struct options *opts) {
    DC_TRACE(env);

    int listener;
    fd_set read_fds;
    int max_fd;
    int client_sockets[MAX_CLIENTS];

    listener = setup_server(env, error, opts);

    if(listener < 0)
    {
        return EXIT_FAILURE;
    }

    max_fd = listener;
    dc_memset(env, client_sockets, 0, sizeof(client_sockets));
    dc_signal(env, error, SIGINT, ctrl_c_handler);
    run_server(env, error, listener, client_sockets, &read_fds, &max_fd,opts);
    dc_close(env, error, listener);

    return EXIT_SUCCESS;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static void ctrl_c_handler(__attribute__((unused)) int signum)
{
    done = true;
}
#pragma GCC diagnostic pop

static int setup_server(struct dc_env *env, struct dc_error *err, struct options *opts)
{
    int listener;
    int option_value;
    struct sockaddr_in server_addr;

    DC_TRACE(env);
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

    if(dc_listen(env, err, listener, MAX_PENDING) < 0)
    {
        dc_perror(env, "listen");
        dc_close(env, err, listener);
        return -1;
    }
    printf("Setup Select Server and awaiting connection\n");

    return listener;
}

static int run_server(struct dc_env *env, struct dc_error *err, int listener, int *clients, fd_set *read_fds, int *max_fd,struct options *opts)
{
    DC_TRACE(env);

    while(!(done))
    {
        int ready;

        ready = wait_for_data(env, err, listener, clients, read_fds, max_fd);

        if(ready < 0)
        {
            dc_perror(env, "select");
            continue;
        }

        handle_new_connections(env, err, listener, clients, read_fds, max_fd, opts);
        handle_client_data(env, err, clients, read_fds,  opts);
    }

    return EXIT_SUCCESS;
}

static int wait_for_data(struct dc_env *env, struct dc_error *err, int listener, const int *clients, fd_set *read_fds, const int *max_fd)
{
    DC_TRACE(env);
    FD_ZERO(read_fds);
    FD_SET(listener, read_fds);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] > 0)
        {
            FD_SET(clients[i], read_fds);
        }
    }

    return dc_select(env, err, *max_fd + 1, read_fds, NULL, NULL, NULL);
}

static void handle_new_connections(struct dc_env *env, struct dc_error *err, int listener, int *clients, fd_set *read_fds, int *max_fd, struct options *opts)
{
    DC_TRACE(env);

    if (FD_ISSET(listener, read_fds))
    {

        struct sockaddr_in client_addr;
        socklen_t client_len;
        int client_fd;

        dc_memset(env, &client_addr, 0, sizeof(client_addr));
        client_len = sizeof(client_addr);
        client_fd = dc_accept(env, err, listener, (struct sockaddr*) &client_addr, &client_len);
        printf("Number %d\n", client_fd);
        if(client_fd < 0)
        {
            dc_perror(env, "accept");
            return;
        }

        printf("New connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));    // NOLINT(concurrency-mt-unsafe)
        opts->time = clock();
        for(int i = 0; i < MAX_CLIENTS; i++)
        {
            if(clients[i] == 0)
            {
                clients[i] = client_fd;
                break;
            }
        }

        if (client_fd > *max_fd)
        {
            *max_fd = client_fd;
        }
    }
}

static void handle_client_data(struct dc_env *env, struct dc_error *err, int *clients, fd_set* read_fds, struct options *opts)
{
    char buffer[BUF_SIZE];

    DC_TRACE(env);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] > 0 && FD_ISSET(clients[i], read_fds))
        {
            ssize_t bytes_read;

            bytes_read = dc_read(env, err, clients[i], buffer, BUF_SIZE);

            if(bytes_read <= 0)
            {
                printf("Client disconnected\n");
                clock_t end = clock();
                double time_spent = ((double)(end - opts->time) / CLOCKS_PER_SEC) * CONVERT_TO_MS;
                write_to_file(opts, "Select Server", "handle_data", time_spent);

                dc_close(env, err, clients[i]);
                clients[i] = 0;
                continue;
            }

            printf("Read from client\n");
//            dc_write(env, err, STDOUT_FILENO, buffer, bytes_read);


            printf("Writing to client\n");
            uint16_t write_number = ntohs(bytes_read);
            dc_write(env, err, clients[i], &write_number, sizeof(write_number));

        }
    }
}
