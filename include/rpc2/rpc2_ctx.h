#pragma once

#include "rpc2_framing.h"
#include "rpc2_request.h"

#include <libubox/list.h>
#include <libubox/runqueue.h>
#include <libubox/uloop.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

struct rpc_ctx;

typedef bool (*rpc_handler_fn)(struct rpc_request * req);
typedef void (*rpc_timeout_fn)(struct rpc_request * req);

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

struct rpc_ctx * rpc_ctx_new(void);
void rpc_ctx_destroy(struct rpc_ctx * ctx);

/* ── Configuration ──────────────────────────────────────────────────────── */

void rpc_ctx_set_fds(struct rpc_ctx * ctx, int in_fd, int out_fd);
void rpc_ctx_set_framing(struct rpc_ctx * ctx, struct framing_st * framing);

void rpc_ctx_set_can_exit(struct rpc_ctx * ctx, bool (*cb)(struct rpc_ctx * ctx, void * user_data), void * user_data);

/* ── Method registration ────────────────────────────────────────────────── */

void rpc_add_handler(
    struct rpc_ctx * ctx,
    char const * method,
    rpc_handler_fn handler,
    unsigned int timeout_ms,
    rpc_timeout_fn on_timeout,
    void * handler_data
);

/* ── Process execution ──────────────────────────────────────────────────── */

void rpc_ctx_set_max_processes(struct rpc_ctx * ctx, int max);
void rpc_ctx_close_stdin(struct rpc_ctx * ctx);

/* ── Run loop ───────────────────────────────────────────────────────────── */

void rpc_ctx_run(struct rpc_ctx * ctx);
void rpc_ctx_stop(struct rpc_ctx * ctx);

/* ── Internal (used by other rpc2_*.c modules) ─────────────────────────── */

void rpc_ctx_send_json(struct rpc_ctx * ctx, struct json_object * msg);
void rpc_ctx_send_error(struct rpc_ctx * ctx, struct json_object * id, int code, char const * message);
void rpc_ctx_process_add(struct rpc_ctx * ctx, struct runqueue_process * proc, pid_t pid);
struct runqueue * rpc_ctx_get_queue(struct rpc_ctx * ctx);
