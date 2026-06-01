#include "rpc2_ctx.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UNUSED_PARAM(p) ((void)(p))

/* ── Method entry ───────────────────────────────────────────────────────── */

typedef struct rpc_method_st
{
    char * name;
    rpc_handler_fn handler;
    unsigned int timeout_ms;
    rpc_timeout_fn on_timeout;
    void * handler_data;
} rpc_method_st;

typedef struct rpc_method_registry_st
{
    rpc_method_st * methods;
    size_t count;
    size_t capacity;
} rpc_method_registry_st;

/* ── Write queue entry ──────────────────────────────────────────────────── */

typedef struct write_queue_entry_st
{
    struct list_head list;
    char * buf;
    size_t len;
    size_t pos;
} write_queue_entry_st;

/* ── Context struct ─────────────────────────────────────────────────────── */

struct rpc_ctx
{
    int in_fd;
    int out_fd;
    struct uloop_fd stdin_fd;
    struct uloop_fd out_uloop_fd;
    struct list_head write_queue;
    char * buf;
    size_t buf_len;
    size_t buf_cap;
    struct framing_st * framing;
    bool eof_reached;

    rpc_method_registry_st registry;
    struct list_head pending_requests;

    struct runqueue process_queue;

    bool (*can_exit)(struct rpc_ctx * ctx, void * user_data);
    void * can_exit_data;
};

/* ── Forward declarations ───────────────────────────────────────────────── */

static void check_exit_condition(struct rpc_ctx * ctx);

/* ── Transport buffer helper ────────────────────────────────────────────── */

static bool
append_to_buffer(struct rpc_ctx * ctx, char const * data, size_t data_len)
{
    size_t needed = ctx->buf_len + data_len;

    if (needed > ctx->buf_cap)
    {
        size_t new_cap = ctx->buf_cap == 0 ? 4096 : ctx->buf_cap * 2;

        while (new_cap < needed)
        {
            new_cap *= 2;
        }
        char * new_buf = realloc(ctx->buf, new_cap);

        if (!new_buf)
        {
            perror("realloc");
            return false;
        }
        ctx->buf = new_buf;
        ctx->buf_cap = new_cap;
    }
    memcpy(ctx->buf + ctx->buf_len, data, data_len);
    ctx->buf_len = needed;
    ctx->buf[ctx->buf_len] = '\0';
    return true;
}

/* ── JSON-RPC dispatch ──────────────────────────────────────────────────── */

static void
rpc_dispatch(struct rpc_ctx * ctx, struct json_object * msg)
{
    struct json_object * id = NULL;
    struct json_object * method_obj = NULL;
    struct json_object * params = NULL;
    struct json_object * version = NULL;

    if (!json_object_is_type(msg, json_type_object))
    {
        fprintf(stderr, "[RPC] Error: message is not a JSON object\n");
        return;
    }

    if (!json_object_object_get_ex(msg, "jsonrpc", &version) || strcmp(json_object_get_string(version), "2.0") != 0)
    {
        fprintf(stderr, "[RPC] Error: invalid JSON-RPC version\n");
        return;
    }

    json_object_object_get_ex(msg, "id", &id);

    if (!json_object_object_get_ex(msg, "method", &method_obj) || !json_object_is_type(method_obj, json_type_string))
    {
        fprintf(stderr, "[RPC] Error: invalid method\n");
        if (id)
        {
            rpc_ctx_send_error(ctx, id, -32600, "Invalid Request");
        }
        return;
    }

    char const * method_name = json_object_get_string(method_obj);

    json_object_object_get_ex(msg, "params", &params);

    /* Find handler */
    rpc_method_st * matched = NULL;

    for (size_t i = 0; i < ctx->registry.count; i++)
    {
        if (strcmp(ctx->registry.methods[i].name, method_name) == 0)
        {
            matched = &ctx->registry.methods[i];
            break;
        }
    }

    if (!matched)
    {
        if (id)
        {
            fprintf(stderr, "[RPC] Error: method '%s' not found\n", method_name);
            rpc_ctx_send_error(ctx, id, -32601, "Method not found");
        }
        else
        {
            fprintf(stderr, "[RPC] Unhandled notification: %s\n", method_name);
        }
        return;
    }

    /* Create request object */
    struct rpc_request * req = rpc_request_new(ctx, id, params, matched->handler_data);

    if (!req)
    {
        if (id)
        {
            rpc_ctx_send_error(ctx, id, -32603, "Internal error");
        }
        return;
    }

    /* Call handler */
    bool handled = matched->handler(req);

    if (!handled)
    {
        if (id && !req->responded)
        {
            fprintf(stderr, "[RPC] Error: handler failed for '%s'\n", method_name);
            rpc_err(req, -32603, "Internal error");
        }
        else if (!req->responded)
        {
            rpc_request_free(req);
        }
        return;
    }

    /* If handler succeeded and has a timeout, start tracking */
    if (matched->timeout_ms > 0 && id && !req->responded)
    {
        list_add_tail(&req->pending_list, &ctx->pending_requests);
        rpc_request_add_pending(req, matched->timeout_ms, matched->on_timeout);
    }
}

static void
on_transport_msg(char const * body, size_t len, void * user_data)
{
    UNUSED_PARAM(len);
    struct rpc_ctx * ctx = (struct rpc_ctx *)user_data;

    struct json_object * msg = json_tokener_parse(body);

    if (msg)
    {
        rpc_dispatch(ctx, msg);
        json_object_put(msg);
    }
    else
    {
        fprintf(stderr, "[RPC] Error: failed to parse JSON body\n");
    }
}

/* ── Write queue drain ──────────────────────────────────────────────────── */

static void
check_exit_condition(struct rpc_ctx * ctx)
{
    if (!ctx->eof_reached)
    {
        return;
    }
    if (!list_empty(&ctx->write_queue))
    {
        return;
    }
    if (ctx->process_queue.running_tasks > 0)
    {
        return;
    }
    if (ctx->can_exit)
    {
        if (ctx->can_exit(ctx, ctx->can_exit_data))
        {
            uloop_end();
        }
    }
    else
    {
        uloop_end();
    }
}

static void
write_queue_cb(struct uloop_fd * u, unsigned int events)
{
    UNUSED_PARAM(events);
    struct rpc_ctx * ctx = container_of(u, struct rpc_ctx, out_uloop_fd);

    while (!list_empty(&ctx->write_queue))
    {
        write_queue_entry_st * entry = list_first_entry(&ctx->write_queue, write_queue_entry_st, list);
        ssize_t const bytes_written = write(u->fd, entry->buf + entry->pos, entry->len - entry->pos);

        if (bytes_written < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return;
            }
            if (errno == EINTR)
            {
                continue;
            }
            perror("write_queue_cb: write failed");
            uloop_end();
            return;
        }

        entry->pos += (size_t)bytes_written;
        if (entry->pos == entry->len)
        {
            list_del(&entry->list);
            free(entry->buf);
            free(entry);
        }
    }

    uloop_fd_delete(&ctx->out_uloop_fd);
    check_exit_condition(ctx);
}

/* ── stdin callback ─────────────────────────────────────────────────────── */

static void
stdin_cb(struct uloop_fd * u, unsigned int events)
{
    UNUSED_PARAM(events);
    struct rpc_ctx * ctx = container_of(u, struct rpc_ctx, stdin_fd);

    char tmp[4096];
    ssize_t n = read(u->fd, tmp, sizeof(tmp));

    if (n == 0)
    {
        ctx->eof_reached = true;
        uloop_fd_delete(u);
        check_exit_condition(ctx);
        return;
    }

    if (n < 0)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        {
            uloop_fd_delete(u);
            uloop_end();
        }
        return;
    }

    fprintf(stderr, "[LSP] read %zu bytes from stdin\n", (size_t)n);

    if (!append_to_buffer(ctx, tmp, (size_t)n))
    {
        uloop_end();
        return;
    }

    for (;;)
    {
        size_t msg_offset, msg_len;
        frame_decode_result_t r = ctx->framing->decode(ctx->framing, ctx->buf, ctx->buf_len, &msg_offset, &msg_len);

        if (r == FRAME_NEED_MORE)
        {
            break;
        }

        if (r == FRAME_ERROR)
        {
            uloop_end();
            return;
        }

        fprintf(stderr, "[LSP] decoded %zu-byte frame\n", msg_len);

        char saved = ctx->buf[msg_offset + msg_len];

        ctx->buf[msg_offset + msg_len] = '\0';
        on_transport_msg(ctx->buf + msg_offset, msg_len, ctx);
        ctx->buf[msg_offset + msg_len] = saved;

        size_t const consumed = msg_offset + msg_len;
        size_t const remaining = ctx->buf_len - consumed;

        memmove(ctx->buf, ctx->buf + consumed, remaining);
        ctx->buf_len = remaining;
        ctx->buf[ctx->buf_len] = '\0';
    }
}

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

struct rpc_ctx *
rpc_ctx_new(void)
{
    struct rpc_ctx * ctx = calloc(1, sizeof(*ctx));

    if (!ctx)
    {
        return NULL;
    }

    INIT_LIST_HEAD(&ctx->write_queue);
    INIT_LIST_HEAD(&ctx->pending_requests);

    ctx->in_fd = -1;
    ctx->out_fd = -1;

    return ctx;
}

void
rpc_ctx_destroy(struct rpc_ctx * ctx)
{
    if (!ctx)
    {
        return;
    }

    /* Cancel all pending requests */
    while (!list_empty(&ctx->pending_requests))
    {
        struct rpc_request * req = list_first_entry(&ctx->pending_requests, struct rpc_request, pending_list);

        list_del_init(&req->pending_list);
        rpc_request_free(req);
    }

    /* Free method registry */
    for (size_t i = 0; i < ctx->registry.count; i++)
    {
        free(ctx->registry.methods[i].name);
    }
    free(ctx->registry.methods);

    /* Kill process queue */
    runqueue_kill(&ctx->process_queue);

    /* Free framing */
    if (ctx->framing)
    {
        ctx->framing->destroy(ctx->framing);
    }

    /* Free transport buffer */
    free(ctx->buf);

    free(ctx);
}

/* ── Configuration ──────────────────────────────────────────────────────── */

void
rpc_ctx_set_fds(struct rpc_ctx * ctx, int in_fd, int out_fd)
{
    int flags;

    ctx->in_fd = in_fd;
    ctx->out_fd = out_fd;

    flags = fcntl(out_fd, F_GETFL, 0);
    fcntl(out_fd, F_SETFL, flags | O_NONBLOCK);

    ctx->stdin_fd.fd = in_fd;
    ctx->stdin_fd.cb = stdin_cb;
    uloop_fd_add(&ctx->stdin_fd, ULOOP_READ);

    ctx->out_uloop_fd.fd = out_fd;
    ctx->out_uloop_fd.cb = write_queue_cb;
}

void
rpc_ctx_set_framing(struct rpc_ctx * ctx, struct framing_st * framing)
{
    ctx->framing = framing;
}

void
rpc_ctx_set_can_exit(struct rpc_ctx * ctx, bool (*cb)(struct rpc_ctx * ctx, void * user_data), void * user_data)
{
    ctx->can_exit = cb;
    ctx->can_exit_data = user_data;
}

void
rpc_ctx_set_max_processes(struct rpc_ctx * ctx, int max)
{
    ctx->process_queue.max_running_tasks = max;
}

void
rpc_ctx_close_stdin(struct rpc_ctx * ctx)
{
    uloop_fd_delete(&ctx->stdin_fd);
    ctx->eof_reached = true;
    check_exit_condition(ctx);
}

void
rpc_ctx_run(struct rpc_ctx * ctx)
{
    if (!ctx->framing)
    {
        fprintf(stderr, "[RPC] Error: no framing strategy set\n");
        return;
    }

    uloop_init();
    runqueue_init(&ctx->process_queue);
    ctx->process_queue.max_running_tasks = 4;

    uloop_run();

    uloop_done();
}

void
rpc_ctx_stop(struct rpc_ctx * ctx)
{
    UNUSED_PARAM(ctx);
    uloop_end();
}

/* ── Method registration ────────────────────────────────────────────────── */

void
rpc_add_handler(
    struct rpc_ctx * ctx,
    char const * method,
    rpc_handler_fn handler,
    unsigned int timeout_ms,
    rpc_timeout_fn on_timeout,
    void * handler_data
)
{
    if (ctx->registry.count == ctx->registry.capacity)
    {
        ctx->registry.capacity = ctx->registry.capacity == 0 ? 8 : ctx->registry.capacity * 2;
        ctx->registry.methods = realloc(ctx->registry.methods, ctx->registry.capacity * sizeof(*ctx->registry.methods));
    }

    size_t const idx = ctx->registry.count;

    ctx->registry.methods[idx].name = strdup(method);
    ctx->registry.methods[idx].handler = handler;
    ctx->registry.methods[idx].timeout_ms = timeout_ms;
    ctx->registry.methods[idx].on_timeout = on_timeout;
    ctx->registry.methods[idx].handler_data = handler_data;
    ctx->registry.count++;
}

/* ── JSON sending ───────────────────────────────────────────────────────── */

void
rpc_ctx_send_json(struct rpc_ctx * ctx, struct json_object * msg)
{
    char const * json_str = json_object_to_json_string_ext(msg, JSON_C_TO_STRING_PLAIN);
    size_t framed_len;
    char * framed = ctx->framing->encode(ctx->framing, json_str, strlen(json_str), &framed_len);

    if (!framed)
    {
        return;
    }

    write_queue_entry_st * entry = malloc(sizeof(*entry));

    if (!entry)
    {
        free(framed);
        return;
    }

    entry->len = framed_len;
    entry->buf = framed;
    entry->pos = 0;

    bool const was_empty = list_empty(&ctx->write_queue);

    list_add_tail(&entry->list, &ctx->write_queue);

    if (was_empty)
    {
        uloop_fd_add(&ctx->out_uloop_fd, ULOOP_WRITE);
        write_queue_cb(&ctx->out_uloop_fd, ULOOP_WRITE);
    }
}

void
rpc_ctx_send_error(struct rpc_ctx * ctx, struct json_object * id, int code, char const * message)
{
    struct json_object * msg = json_object_new_object();

    json_object_object_add(msg, "jsonrpc", json_object_new_string("2.0"));

    struct json_object * error = json_object_new_object();

    json_object_object_add(error, "code", json_object_new_int(code));
    json_object_object_add(error, "message", json_object_new_string(message));
    json_object_object_add(msg, "error", error);

    if (id)
    {
        json_object_object_add(msg, "id", json_object_get(id));
    }
    else
    {
        json_object_object_add(msg, "id", NULL);
    }

    rpc_ctx_send_json(ctx, msg);
    json_object_put(msg);
}

/* ── Process execution (internal) ───────────────────────────────────────── */

void
rpc_ctx_process_add(struct rpc_ctx * ctx, struct runqueue_process * proc, pid_t pid)
{
    runqueue_process_add(&ctx->process_queue, proc, pid);
}

struct runqueue *
rpc_ctx_get_queue(struct rpc_ctx * ctx)
{
    return &ctx->process_queue;
}
