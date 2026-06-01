#pragma once

#include <libubox/uloop.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct rpc_ctx rpc_ctx;

typedef struct rpc_timer
{
    struct uloop_timeout tm;
    void (*cb)(struct rpc_timer * t, void * user_data);
    void * user_data;
} rpc_timer;

void rpc_timer_init(rpc_timer * t, void (*cb)(rpc_timer * t, void * user_data), void * user_data);
void rpc_timer_start(rpc_ctx * ctx, rpc_timer * t, unsigned int delay_ms);
void rpc_timer_cancel(rpc_timer * t);
