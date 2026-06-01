#pragma once

#include "rpc2/rpc2_request.h"

#include <json-c/json.h>
#include <libubox/list.h>
#include <libubox/uloop.h>
#include <stdbool.h>

struct rpc_request {
  struct rpc_ctx *ctx;
  struct json_object *id;
  struct json_object *params;

  void *handler_data;

  void *user_data;
  void (*user_data_cleanup)(void *data);

  struct list_head pending_list;
  struct uloop_timeout timeout_timer;
  void (*timeout_cb)(struct rpc_request *);
  bool timeout_active;

  bool responded;
};

struct rpc_request *rpc_request_new(struct rpc_ctx *ctx, struct json_object *id,
                                    struct json_object *params,
                                    void *handler_data);
void rpc_request_free(struct rpc_request *req);

void rpc_request_add_pending(struct rpc_request *req, unsigned int timeout_ms,
                             void (*timeout_cb)(struct rpc_request *));
void rpc_request_cancel_pending(struct rpc_request *req);
