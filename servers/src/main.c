#include "server.h"
#include "util.h"
#include <arpa/inet.h>
#include <dc_c/dc_string.h>
#include <dc_env/env.h>
#include <dc_error/error.h>
#include <memory.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_PORT 5000

/**
 * Parse the cmd line arguments and setup the options struct.
 * @param env Environment object.
 * @param error Error object.
 * @param argc Number of cmd line arguments.
 * @param argv Cmd line arguments.
 * @param opts Options object.
 */
static int parse_arguments(struct dc_env * env, struct dc_error * error, int argc, char *argv[], struct options *opts);
/**
 * Initializes the options object to default values.
 * @param env Environment object.
 * @param opts Options object.
 */
static void options_init(struct dc_env * env, struct options *opts);
/**
 * Runs the corresponding server or client based on options object.
 * @param env Environment object.
 * @param error Error object.
 * @param opts Options object.
 * @return Return status of the server/client
 */
static int run_corresponding_server(struct dc_env * env, struct dc_error * error, struct options *opts);
/**
 * Frees any dynamically allocated memory.
 * @param error Error object.
 */
static void close_server(struct dc_error * error);

int main(int argc, char * argv[])
{
    dc_env_tracer tracer;
    struct dc_error * err;
    struct dc_env * env;
    struct options opts;
    int exit_status = 0;
    int return_value;
    // Set the tracer to trace through the function calls
    //tracer = dc_env_default_tracer; // Trace through function calls
    tracer = NULL; // Don't trace through function calls

    err = dc_error_create(false); // Create error struct
    env = dc_env_create(err, false, tracer); // Create environment struct


    dc_env_set_tracer(env, *tracer); // Set tracer
    // Initialize options struct
    options_init(env, &opts);
    // Parse cmd line arguments
    return_value = parse_arguments(env, err, argc, argv, &opts);

    if (!return_value) {
        run_corresponding_server(env, err, &opts);
    }

    close_server(err);

    // Free memory
    dc_error_reset(err);
    free(err);
    free(env);

    return exit_status;
}

static int run_corresponding_server(struct dc_env * env, struct dc_error * error, struct options *opts) {
    int exit_status = -1;

    switch (opts->server_to_run)
    {
        case ONE_TO_ONE: {
            exit_status = run_normal_server(env, error, opts);
            return exit_status;
        }
        case POLL_SERVER:
        {
            exit_status = run_poll_server(env, error, opts);
            return exit_status;
        }
        case SELECT_SERVER:
        {
            exit_status = run_select_server(env, error, opts);
            return exit_status;
        }
        case THREAD_POLL_SERVER:
        {
            exit_status = run_thread_poll_server(env, error, opts);
            return exit_status;
        }
        default:{
            DC_ERROR_RAISE_USER(error, "Invalid Specified Server Type\n", 1);
            return exit_status;
        }
    }
}

static void options_init(struct dc_env * env, struct options *opts)
{
    dc_memset(env, opts, 0, sizeof(struct options));
    opts->port_out = DEFAULT_PORT;
}

static int parse_arguments(struct dc_env * env, struct dc_error * error, int argc, char *argv[], struct options *opts)
{
    // Ensure ip address and run flag is given in order to run
    if (argc <= 2) {
        DC_ERROR_RAISE_USER(error, "Please give an IP Address as first argument and the run flag as the second "
                                   "(o -> one-to-one server, p -> poll server, s -> select server) [t -> truncate csv file]\n", 1);
        return -1;
    }

    // Check ip address
    if (inet_addr(argv[1]) == ( in_addr_t)(-1)) {
        DC_ERROR_RAISE_USER(error, "Invalid IP Address", 1);
        return -1;
    }

    printf("Listening on ip address: %s \n", argv[1]);
    printf("Port number: %d \n\n", opts->port_out);
    opts->ip_address = argv[1];

    // Check to see what server to run
    if (dc_strcmp(env, argv[2], "o") == 0) {
        opts->server_to_run = ONE_TO_ONE;
    } else if (dc_strcmp(env, argv[2], "p") == 0) {
        opts->server_to_run = POLL_SERVER;
    } else if (dc_strcmp(env, argv[2], "s") == 0) {
        opts->server_to_run = SELECT_SERVER;
    } else if (dc_strcmp(env, argv[2], "t") == 0) {
        opts->server_to_run = THREAD_POLL_SERVER;
    } else {
        DC_ERROR_RAISE_USER(error, "Invalid Server Type (o -> one-to-one server, p -> poll server)\n", -1);
        return -1;
    }

    // Optional truncate csv file
    if (argc == 4) {
        if (dc_strcmp(env, argv[3], "t") == 0) {
            opts->csv_file = fopen("states.csv", "we");
        }
    }

    // Open CSV File if not already open
    if (!opts->csv_file) {
        opts->csv_file = fopen("states.csv", "ae");
    }

    return 0;
}

static void close_server(struct dc_error * error) {
    if (dc_error_has_error(error)) {
        fprintf(stderr, "ERROR: %s \n", dc_error_get_message(error)); //NOLINT(cert-err33-c)
    }
}
