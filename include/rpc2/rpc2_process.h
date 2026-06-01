#pragma once

#include <libubox/runqueue.h>
#include <libubox/uloop.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct rpc_ctx rpc_ctx;

typedef struct rpc_process
{
    struct runqueue_process run_proc;
    struct uloop_fd pipe_fd;
    char * output;
    size_t output_len;
    bool running;

    /* internal - set by rpc_process_start */
    rpc_ctx * ctx;
    void (*on_complete)(rpc_ctx * ctx, struct rpc_process * p, void * user_data);
    void * user_data;
} rpc_process;

void rpc_process_init(rpc_process * p);

bool rpc_process_start(
    rpc_ctx * ctx,
    rpc_process * p,
    char const * path,
    char ** argv,
    void (*on_complete)(rpc_ctx * ctx, rpc_process * p, void * user_data),
    void * user_data
);

void rpc_process_kill(rpc_process * p);

char const * rpc_process_output(rpc_process const * p);
size_t rpc_process_output_len(rpc_process const * p);
