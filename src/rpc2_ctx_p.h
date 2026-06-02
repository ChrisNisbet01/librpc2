#pragma once

#include "rpc2/rpc2_ctx.h"

#include <json-c/json.h>
#include <libubox/list.h>
#include <libubox/runqueue.h>
#include <sys/types.h>

void rpc_ctx_send_json(struct rpc_ctx * ctx, struct json_object * msg);
void rpc_ctx_send_error(struct rpc_ctx * ctx, struct json_object * id, int code, char const * message);
void rpc_ctx_process_add(struct rpc_ctx * ctx, struct runqueue_process * proc, pid_t pid);
struct runqueue * rpc_ctx_get_queue(struct rpc_ctx * ctx);
