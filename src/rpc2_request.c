#include "rpc2_request.h"

#include "rpc2_ctx.h"

#include <json-c/json.h>
#include <libubox/list.h>
#include <libubox/uloop.h>
#include <stdlib.h>
#include <string.h>

/* The struct rpc_request is defined in rpc2_request.h */

/* ── Internal ───────────────────────────────────────────────────────────── */

struct rpc_request *
rpc_request_new(struct rpc_ctx * ctx, struct json_object * id, struct json_object * params, void * handler_data)
{
    struct rpc_request * req = calloc(1, sizeof(*req));

    if (!req)
    {
        return NULL;
    }

    INIT_LIST_HEAD(&req->pending_list);
    req->ctx = ctx;
    req->id = id ? json_object_get(id) : NULL;
    req->params = params ? json_object_get(params) : NULL;
    req->handler_data = handler_data;

    return req;
}

void
rpc_request_free(struct rpc_request * req)
{
    if (!req)
    {
        return;
    }

    if (req->user_data_cleanup)
    {
        req->user_data_cleanup(req->user_data);
    }

    rpc_request_cancel_pending(req);

    json_object_put(req->id);
    json_object_put(req->params);
    free(req);
}

static void
timeout_timer_cb(struct uloop_timeout * tm)
{
    struct rpc_request * req = container_of(tm, struct rpc_request, timeout_timer);

    req->timeout_active = false;

    if (!list_empty(&req->pending_list))
    {
        list_del_init(&req->pending_list);
    }

    if (req->timeout_cb)
    {
        req->timeout_cb(req);
    }

    if (!req->responded)
    {
        rpc_request_free(req);
    }
}

void
rpc_request_add_pending(struct rpc_request * req, unsigned int timeout_ms, void (*timeout_cb)(struct rpc_request *))
{
    if (req->responded)
    {
        return;
    }

    req->timeout_cb = timeout_cb;
    req->timeout_timer.cb = timeout_timer_cb;
    req->timeout_active = true;
    uloop_timeout_set(&req->timeout_timer, (int)timeout_ms);
}

void
rpc_request_cancel_pending(struct rpc_request * req)
{
    if (req->timeout_active)
    {
        uloop_timeout_cancel(&req->timeout_timer);
        req->timeout_active = false;
    }

    if (!list_empty(&req->pending_list))
    {
        list_del_init(&req->pending_list);
    }
}

/* ── Public API ─────────────────────────────────────────────────────────── */

struct json_object *
rpc_params(struct rpc_request * req)
{
    return req->params;
}

void *
rpc_handler_data(struct rpc_request * req)
{
    return req->handler_data;
}

struct rpc_ctx *
rpc_request_ctx(struct rpc_request * req)
{
    return req->ctx;
}

void
rpc_set_userdata(struct rpc_request * req, void * data, rpc_userdata_cleanup cleanup)
{
    req->user_data = data;
    req->user_data_cleanup = cleanup;
}

void *
rpc_get_userdata(struct rpc_request * req)
{
    return req->user_data;
}

void
rpc_ok(struct rpc_request * req, struct json_object * result)
{
    if (req->responded)
    {
        json_object_put(result);
        return;
    }

    req->responded = true;

    rpc_request_cancel_pending(req);

    struct json_object * msg = json_object_new_object();

    json_object_object_add(msg, "jsonrpc", json_object_new_string("2.0"));
    json_object_object_add(msg, "result", result);

    if (req->id)
    {
        json_object_object_add(msg, "id", json_object_get(req->id));
    }
    else
    {
        json_object_object_add(msg, "id", NULL);
    }

    rpc_ctx_send_json(req->ctx, msg);
    json_object_put(msg);
    rpc_request_free(req);
}

void
rpc_err(struct rpc_request * req, int code, char const * message)
{
    if (req->responded)
    {
        return;
    }

    req->responded = true;

    rpc_request_cancel_pending(req);

    struct json_object * msg = json_object_new_object();

    json_object_object_add(msg, "jsonrpc", json_object_new_string("2.0"));

    struct json_object * error = json_object_new_object();

    json_object_object_add(error, "code", json_object_new_int(code));
    json_object_object_add(error, "message", json_object_new_string(message));
    json_object_object_add(msg, "error", error);

    if (req->id)
    {
        json_object_object_add(msg, "id", json_object_get(req->id));
    }
    else
    {
        json_object_object_add(msg, "id", NULL);
    }

    rpc_ctx_send_json(req->ctx, msg);
    json_object_put(msg);
    rpc_request_free(req);
}
