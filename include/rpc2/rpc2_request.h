#pragma once

#include <json-c/json.h>
#include <libubox/list.h>
#include <libubox/uloop.h>
#include <stdbool.h>
#include <stddef.h>

struct rpc_ctx;

/* ── rpc_request struct (visible for library internal use) ──────────────── */
/* Public API users should treat this as opaque and use the accessor functions. */

struct rpc_request
{
    struct rpc_ctx * ctx;
    struct json_object * id;
    struct json_object * params;

    void * handler_data;

    void * user_data;
    void (*user_data_cleanup)(void * data);

    struct list_head pending_list;
    struct uloop_timeout timeout_timer;
    void (*timeout_cb)(struct rpc_request *);
    bool timeout_active;

    bool responded;
};

typedef void (*rpc_userdata_cleanup)(void * data);

/* ── Public API ─────────────────────────────────────────────────────────── */

struct json_object * rpc_params(struct rpc_request * req);
void * rpc_handler_data(struct rpc_request * req);
struct rpc_ctx * rpc_request_ctx(struct rpc_request * req);

void rpc_set_userdata(struct rpc_request * req, void * data, rpc_userdata_cleanup cleanup);
void * rpc_get_userdata(struct rpc_request * req);

void rpc_ok(struct rpc_request * req, struct json_object * result);
void rpc_err(struct rpc_request * req, int code, char const * message);

/* ── Internal (used by other rpc2_*.c modules) ─────────────────────────── */

struct rpc_request *
rpc_request_new(struct rpc_ctx * ctx, struct json_object * id, struct json_object * params, void * handler_data);
void rpc_request_free(struct rpc_request * req);

void
rpc_request_add_pending(struct rpc_request * req, unsigned int timeout_ms, void (*timeout_cb)(struct rpc_request *));
void rpc_request_cancel_pending(struct rpc_request * req);
