#include "client.h"
#include "util.h"
#include <arpa/inet.h>
#include <assert.h>
#include <dc_c/dc_signal.h>
#include <dc_c/dc_string.h>
#include <dc_env/env.h>
#include <dc_error/error.h>
#include <getopt.h>
#include <memory.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_PORT 5000

static void ctrl_c_handler(__attribute__((unused)) int signum);
static void parse_arguments(struct dc_error * error, int argc, char *argv[], struct options *opts);
static void options_init(struct dc_env * env, struct options *opts);
static void close_server(struct dc_error * error);

static volatile sig_atomic_t done = false;   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

int main(int argc, char * argv[])
{
    dc_env_tracer tracer;
    struct dc_error * err;
    struct dc_env * env;
    struct options opts;
    int exit_status = 0;
    // Set the tracer to trace through the function calls
    //tracer = dc_env_default_tracer; // Trace through function calls
    tracer = NULL; // Don't trace through function calls

    err = dc_error_create(false); // Create error struct
    env = dc_env_create(err, false, tracer); // Create environment struct


    dc_env_set_tracer(env, *tracer); // Set tracer
    // Initialize options struct
    options_init(env, &opts);
    // Parse cmd line arguments
    parse_arguments(err, argc, argv, &opts);

    if (argc >= 1) {
        dc_signal(env, err, SIGINT, ctrl_c_handler);

        exit_status = run_client(&opts);
    }

    close_server(err);

    // Free memory
    dc_error_reset(err);
    free(err);
    free(env);

    return exit_status;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static void ctrl_c_handler(__attribute__((unused)) int signum)
{
    done = 1;
}
#pragma GCC diagnostic pop

static void options_init(struct dc_env * env, struct options *opts)
{

    dc_memset(env, opts, 0, sizeof(struct options));
    opts->port_out = DEFAULT_PORT;
}

static void parse_arguments(struct dc_error * error, int argc, char *argv[], struct options *opts)
{
    int c;

    // Ensure flags are given in order to run
    if (argc <= 1) {
        DC_ERROR_RAISE_USER(error, "No Flags Given", 1);
    }
    while((c = getopt(argc, argv, ":c:p:")) != -1)   // NOLINT(concurrency-mt-unsafe)
    {
        switch(c)
        {

            case 'c':
            {
                if (inet_addr(optarg) == ( in_addr_t)(-1)) {
                    DC_ERROR_RAISE_USER(error, "Invalid IP Address", 1);
                }
                printf("Listening on ip address: %s \n", optarg);
                opts->ip_address = optarg;
                break;
            }
            case 'p':
            {
                printf("Listening on port: %s \n", optarg);
                opts->port_out = parse_port(optarg, 10); // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
                break;
            }
            case ':':
            {
                DC_ERROR_RAISE_USER(error, "Option requires an operand", 1);
            }
            case '?':
            {
                DC_ERROR_RAISE_USER(error, "Unknown", 1);
            }
            default:
            {
                assert("should not get here");
            }
        }
    }

}

static void close_server(struct dc_error * error) {
    if (dc_error_has_error(error)) {
        fprintf(stderr, "ERROR: %s \n", dc_error_get_message(error)); //NOLINT(cert-err33-c)
    }
}
