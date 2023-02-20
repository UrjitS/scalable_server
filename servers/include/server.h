#ifndef SCALABLE_SERVER_SERVER_H
#define SCALABLE_SERVER_SERVER_H

#include "util.h"
#include <dc_env/env.h>
#include <dc_error/error.h>

int run_normal_server(struct dc_env * env, struct dc_error * error, struct options *opts);
int run_poll_server(struct dc_env * env, struct dc_error * error, struct options *opts);
int run_select_server(struct dc_env * env, struct dc_error * error, struct options *opts);
int run_thread_poll_server(struct dc_env * env, struct dc_error * error, struct options *opts);

#endif //SCALABLE_SERVER_SERVER_H
