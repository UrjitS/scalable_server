#ifndef SCALABLE_SERVER_UTIL_H
#define SCALABLE_SERVER_UTIL_H

#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdio.h>
#include <dc_c/dc_stdio.h>
#include <dc_c/dc_stdlib.h>
#include <dc_c/dc_string.h>
#include <dc_posix/arpa/dc_inet.h>
#include <dc_posix/dc_dlfcn.h>
#include <dc_posix/dc_poll.h>
#include <dc_posix/dc_semaphore.h>
#include <dc_posix/dc_signal.h>
#include <dc_posix/dc_string.h>
#include <dc_posix/dc_unistd.h>
#include <dc_posix/sys/dc_select.h>
#include <dc_posix/sys/dc_socket.h>
#include <dc_posix/sys/dc_wait.h>
#include <dc_util/networking.h>
#include <dc_util/system.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dc_util/io.h>

#define READ_BUFFER_SIZE 1024
#define CONVERT_TO_MS 1000
#define BACKLOG SOMAXCONN
static const int BLOCK_SIZE = 1024 * 4;

enum server_types {
    ONE_TO_ONE,
    POLL_SERVER,
    SELECT_SERVER,
    THREAD_POLL_SERVER
};

struct options
{
    /**
     * IP Address of device
     */
    char * ip_address;
    /**
     * File to write the server states to.
     */
    FILE * csv_file;
    /**
     * Type of server to run
     */
    enum server_types server_to_run;
    /**
     * Port to run on
     */
    in_port_t port_out;
    clock_t time;
};

void write_to_file(struct options *opts, const char * server_name, const char * function_name, double time_taken);
void send_message_handler(const struct dc_env *env, struct dc_error *err, __attribute__((unused)) uint8_t *buffer, size_t count, int client_socket, bool *closed);
size_t process_message_handler(const struct dc_env *env, struct dc_error *err, const uint8_t *raw_data, uint8_t **processed_data, ssize_t count);
ssize_t read_message_handler(const struct dc_env *env, struct dc_error *err, uint8_t **raw_data, int client_socket);

#endif //SCALABLE_SERVER_UTIL_H
