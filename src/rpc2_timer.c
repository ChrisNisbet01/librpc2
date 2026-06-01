#include "rpc2_timer.h"

#include <libubox/uloop.h>

static void
timer_cb(struct uloop_timeout * tm)
{
    rpc_timer * t = container_of(tm, rpc_timer, tm);

    if (t->cb)
    {
        t->cb(t, t->user_data);
    }
}

void
rpc_timer_init(rpc_timer * t, void (*cb)(rpc_timer * t, void * user_data), void * user_data)
{
    t->cb = cb;
    t->user_data = user_data;
    t->tm.cb = timer_cb;
}

void
rpc_timer_start(rpc_ctx * ctx, rpc_timer * t, unsigned int delay_ms)
{
    (void)ctx;
    uloop_timeout_set(&t->tm, delay_ms);
}

void
rpc_timer_cancel(rpc_timer * t)
{
    uloop_timeout_cancel(&t->tm);
}
