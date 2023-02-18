#include "server.h"
#include <stdio.h>

int run_poll_server(struct dc_env * env, struct dc_error * error, struct options *opts) {
    DC_TRACE(env);
    printf("Running on %s", opts->ip_address);
    if (dc_error_has_error(error)) {
        return -1;
    }
    return 0;
}
