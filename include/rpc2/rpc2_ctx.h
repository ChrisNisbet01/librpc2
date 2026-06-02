#pragma once

#include <stdbool.h>
#include <stddef.h>

struct rpc_request;
struct framing_st;
struct rpc_ctx;

typedef bool (*rpc_handler_fn)(struct rpc_request * req);
typedef void (*rpc_timeout_fn)(struct rpc_request * req);

struct rpc_ctx * rpc_ctx_new(void);
void rpc_ctx_destroy(struct rpc_ctx * ctx);

void rpc_ctx_set_fds(struct rpc_ctx * ctx, int in_fd, int out_fd);
void rpc_ctx_set_framing(struct rpc_ctx * ctx, struct framing_st * framing);

void rpc_ctx_set_can_exit(struct rpc_ctx * ctx, bool (*cb)(struct rpc_ctx * ctx, void * user_data), void * user_data);

void rpc_add_handler(
    struct rpc_ctx * ctx,
    char const * method,
    rpc_handler_fn handler,
    unsigned int timeout_ms,
    rpc_timeout_fn on_timeout,
    void * handler_data
);

void rpc_ctx_set_max_processes(struct rpc_ctx * ctx, int max);
void rpc_ctx_close_stdin(struct rpc_ctx * ctx);

void rpc_ctx_run(struct rpc_ctx * ctx);
void rpc_ctx_stop(struct rpc_ctx * ctx);
