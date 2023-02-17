#ifndef SCALABLE_SERVER_UTIL_H
#define SCALABLE_SERVER_UTIL_H

#include <netinet/in.h>
#include <stdbool.h>
#include <bits/types/FILE.h>


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
     * If to run the normal server
     */
    bool run_normal_server;
    /**
     * If to run the poll server
     */
    bool run_poll_server;
    /**
     * Port to run on
     */
    in_port_t port_out;
};

void write_to_file(struct options *opts, const char * server_name, const char * function_name, double time_taken);

#endif //SCALABLE_SERVER_UTIL_H
