#include <arpa/inet.h>
#include <stdio.h>
#include "server.h"
#include <netinet/in.h>
#include <signal.h>
#include <dc_posix/dc_unistd.h>
#include <string.h>
#include <dc_util/io.h>
#include <time.h>

#define READ_BUFFER_SIZE 1024
#define BACKLOG 5
static volatile sig_atomic_t running;   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

/**
 * If any error occurs, free any dynamically allocated memory.
 * @param fd Socket file descriptor.
 * @return error number -1
 */
int close_normal_server(int fd);
/**
 * Set a signal handler.
 * @param error Error object.
 * @param sa Sigaction object.
 */
static void set_signal_handling(struct dc_error * error, struct sigaction *sa);
/**
 * Handle signal action.
 * @param sig Signal number.
 */
static void signal_handler(__attribute__((unused)) int sig);
/**
 * Read client messages and send back the number read.
 * @param env Environment object.
 * @param error Error object.
 * @param read_fd Socket file descriptor to read from.
 */
static void read_client_message(struct dc_env * env, struct dc_error * error, int read_fd);
static void open_csv(struct options *opts);
void handle_connection(struct dc_env *env, struct dc_error *error, int socket_fd, struct options *opts);

int run_normal_server(struct dc_env * env, struct dc_error * error, struct options *opts) {
    // Trace this function
    DC_TRACE(env);

    int socket_fd;
    struct sockaddr_in addr;
    int option;
    int bind_result;
    struct sigaction sa;

    // Create a socket
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    if(socket_fd == -1)
    {
        DC_ERROR_RAISE_USER(error, "Failed to create normal server socket\n", -1);
        return close_normal_server(socket_fd);
    }

    // Setup and set socket options
    addr.sin_family = AF_INET;
    addr.sin_port = htons(opts->port_out);
    addr.sin_addr.s_addr = inet_addr(opts->ip_address);
    if(addr.sin_addr.s_addr ==  (in_addr_t)-1)
    {
        DC_ERROR_RAISE_USER(error, "Failed to set normal server socket_in addr\n", -1);
        return close_normal_server(socket_fd);
    }

    option = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    // Bind to the socket
    bind_result = bind(socket_fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));

    if(bind_result == -1)
    {
        DC_ERROR_RAISE_USER(error, "Failed to bind to socket in normal server\n", -1);
        return close_normal_server(socket_fd);
    }

    // Listen on socket
    bind_result = listen(socket_fd, BACKLOG);

    if(bind_result == -1)
    {
        DC_ERROR_RAISE_USER(error, "Failed to listen on socket in normal server\n", -1);
        return close_normal_server(socket_fd);
    }

    set_signal_handling(error, &sa);
    running = 1;

    while(running)
    {

        handle_connection(env, error, socket_fd, opts);

    }

    return 0;
}

void handle_connection(struct dc_env *env, struct dc_error *error, int socket_fd, struct options *opts)
{
    clock_t beginning_connection = clock();

    char * accept_addr_str;
    in_port_t accept_port;
    struct sockaddr_in accept_addr;
    socklen_t accept_addr_len;
    int accepted_fd;
    int socket_error;
    socklen_t len = sizeof (socket_error);

    printf("Setup server and awaiting connection\n");

    // Accept connection
    (accept_addr_len) = sizeof(accept_addr);
    accepted_fd = accept(socket_fd, &accept_addr, &accept_addr_len);

    if (accepted_fd == -1) {
        // Close server exit
        DC_ERROR_RAISE_USER(error, "Failed to listen on socket in normal server\n", -1);
        running = close_normal_server(socket_fd);
        return;
    }
    // Get connection information
    accept_addr_str = inet_ntoa(accept_addr.sin_addr);  // NOLINT(concurrency-mt-unsafe)
    accept_port = ntohs(accept_addr.sin_port);
    printf("Accepted from %s:%d\n", accept_addr_str, accept_port);

    // Loop until accepted connection closes
    socket_error = 0;
    while (socket_error == 0) {
        // Get time
        clock_t handle_message_beginning = clock();
        open_csv(opts);

        // Read client message and write back
        read_client_message(env, error, accepted_fd);
        getsockopt(accepted_fd, SOL_SOCKET, SO_ERROR, &socket_error, &len);

        // Write time taken to handle read/write
        clock_t handle_message_end = clock();
        double time_spent = ((double)(handle_message_end - handle_message_beginning) / CLOCKS_PER_SEC) * 1000;
        fprintf(opts->csv_file, "%s,%s,%f\n", "Normal Server", "read_client_message", time_spent);
        printf("read_client_message() took %f seconds to execute \n", time_spent);
        fclose(opts->csv_file);
    }

    printf("Closing %s:%d\n", accept_addr_str, accept_port);
    close(accepted_fd);

    // Write time taken to handle connection
    clock_t end = clock();
    double time_spent = ((double)(end - beginning_connection) / CLOCKS_PER_SEC) * 1000;
    if (!opts->csv_file) {
        open_csv(opts);
    }
    fprintf(opts->csv_file, "%s,%s,%f\n", "Normal Server", "handle_connection", time_spent);
    printf("handle_connection() took %f seconds to execute \n", time_spent);
    fclose(opts->csv_file);
}

static void read_client_message(struct dc_env * env, struct dc_error * error, int read_fd) {
    char string_buffer[READ_BUFFER_SIZE];
    ssize_t number_read;
    // Read from socket fd.
    number_read = dc_read(env, error, read_fd, string_buffer, 1023);
    if (dc_error_has_error(error)) {
        DC_ERROR_RAISE_USER(error, "Failed to read\n", 1);
        return;
    }

    number_read--;
    printf("Number read from client %zd \n", number_read);

    // Send the number read
    uint16_t write_number = ntohs(number_read);
    dc_write(env, error, read_fd, &write_number, sizeof(write_number));
    if (dc_error_has_error(error)) {
        DC_ERROR_RAISE_USER(error, "Failed to write\n", 1);
        return;
    }
}

int close_normal_server(int fd) {
    if (fd) {
        close(fd);
    }
    return -1;
}

static void set_signal_handling(struct dc_error * error, struct sigaction *sa)
{
    int result;

    sigemptyset(&sa->sa_mask);
    sa->sa_flags = 0;
    sa->sa_handler = signal_handler;
    result = sigaction(SIGINT, sa, NULL);

    if(result == -1)
    {
        DC_ERROR_RAISE_USER(error, "Failed to set signal handler\n", 2);
    }
}

static void open_csv(struct options *opts) {
    opts->csv_file = fopen("states.csv", "a");
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static void signal_handler(__attribute__((unused)) int sig)
{
    running = 0;
}
#pragma GCC diagnostic pop
