#ifndef SCALABLE_SERVER_UTIL_H
#define SCALABLE_SERVER_UTIL_H

#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>

#define READ_BUFFER_SIZE 1024
#define CONVERT_TO_MS 1000
#define BACKLOG SOMAXCONN

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

#endif //SCALABLE_SERVER_UTIL_H
