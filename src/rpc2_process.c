#include "rpc2_process.h"

#include "rpc2_ctx.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UNUSED_PARAM(p) ((void)(p))

static void
pipe_read_cb(struct uloop_fd * u, unsigned int events)
{
    UNUSED_PARAM(events);
    rpc_process * p = container_of(u, rpc_process, pipe_fd);
    char buf[4096];
    ssize_t n;

    while ((n = read(u->fd, buf, sizeof(buf))) > 0)
    {
        char * new_output = realloc(p->output, p->output_len + (size_t)n + 1);

        if (!new_output)
        {
            perror("realloc");
            return;
        }
        p->output = new_output;
        memcpy(p->output + p->output_len, buf, (size_t)n);
        p->output_len += (size_t)n;
        p->output[p->output_len] = '\0';
    }

    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
    {
        uloop_fd_delete(u);
        close(u->fd);
        u->fd = -1;
    }
}

static void
task_complete_cb(struct runqueue * q, struct runqueue_task * t)
{
    UNUSED_PARAM(q);
    rpc_process * p = container_of(t, rpc_process, run_proc.task);

    if (p->pipe_fd.fd != -1)
    {
        pipe_read_cb(&p->pipe_fd, ULOOP_READ);
        if (p->pipe_fd.fd != -1)
        {
            uloop_fd_delete(&p->pipe_fd);
            close(p->pipe_fd.fd);
            p->pipe_fd.fd = -1;
        }
    }

    p->running = false;

    if (p->on_complete)
    {
        p->on_complete(p->ctx, p, p->user_data);
    }
}

static const struct runqueue_task_type rpc_process_task_type = {
    .run = NULL,
};

void
rpc_process_init(rpc_process * p)
{
    memset(p, 0, sizeof(*p));
    p->pipe_fd.fd = -1;
}

bool
rpc_process_start(
    rpc_ctx * ctx,
    rpc_process * p,
    char const * path,
    char ** argv,
    void (*on_complete)(rpc_ctx * ctx, rpc_process * p, void * user_data),
    void * user_data
)
{
    int pipefds[2];

    if (pipe(pipefds) < 0)
    {
        perror("pipe");
        return false;
    }

    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork");
        close(pipefds[0]);
        close(pipefds[1]);
        return false;
    }

    if (pid == 0)
    {
        close(pipefds[0]);
        if (dup2(pipefds[1], STDOUT_FILENO) < 0)
        {
            _exit(127);
        }
        close(pipefds[1]);
        execv(path, argv);
        perror("execv failed");
        _exit(127);
    }

    close(pipefds[1]);

    int flags = fcntl(pipefds[0], F_GETFL, 0);

    fcntl(pipefds[0], F_SETFL, flags | O_NONBLOCK);

    p->pipe_fd.fd = pipefds[0];
    p->pipe_fd.cb = pipe_read_cb;
    uloop_fd_add(&p->pipe_fd, ULOOP_READ);

    p->ctx = ctx;
    p->on_complete = on_complete;
    p->user_data = user_data;
    p->running = true;
    p->run_proc.task.type = &rpc_process_task_type;
    p->run_proc.task.complete = task_complete_cb;

    rpc_ctx_process_add(ctx, &p->run_proc, pid);

    return true;
}

void
rpc_process_kill(rpc_process * p)
{
    if (!p->running)
    {
        return;
    }

    pid_t pid = p->run_proc.proc.pid;

    if (pid > 0)
    {
        kill(pid, SIGTERM);
    }
    runqueue_task_kill(&p->run_proc.task);
    p->running = false;
}

char const *
rpc_process_output(rpc_process const * p)
{
    return p->output;
}

size_t
rpc_process_output_len(rpc_process const * p)
{
    return p->output_len;
}
