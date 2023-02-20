#include "../include/server.h"

int run_thread_poll_server(struct dc_env * env, struct dc_error * error, struct options *opts)
{
    DC_TRACE(env);
    printf("Running poll thread pool server on %s\n", opts->ip_address);
    if (dc_error_has_error(error)) {
        return -1;
    }
    return 0;
}
