#pragma once

#include <json-c/json.h>
#include <stdbool.h>
#include <stddef.h>

struct rpc_ctx;

struct rpc_request;

typedef void (*rpc_userdata_cleanup)(void *data);

struct json_object *rpc_params(struct rpc_request *req);
void *rpc_handler_data(struct rpc_request *req);
struct rpc_ctx *rpc_request_ctx(struct rpc_request *req);

void rpc_set_userdata(struct rpc_request *req, void *data,
                      rpc_userdata_cleanup cleanup);
void *rpc_get_userdata(struct rpc_request *req);

void rpc_ok(struct rpc_request *req, struct json_object *result);
void rpc_err(struct rpc_request *req, int code, char const *message);
