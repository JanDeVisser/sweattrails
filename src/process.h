/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __PROCESS_H__
#define __PROCESS_H__

#ifdef PROCESS_TEST
#define SLICE_IMPLEMENTATION
#define DA_IMPLEMENTATION
#define FS_IMPLEMENTATION
#define PROCESS_IMPLEMENTATION
#endif /* PROCESS_TEST */

#include <pthread.h>

#include "da.h"
#include "fs.h"
#include "slice.h"

#define PROCESS_PIPE_STDOUT 0
#define PROCESS_PIPE_STDERR 1

typedef int               channel_t;
typedef struct _read_pipe read_pipe_t;
typedef void (*pipe_on_read_t)(struct _read_pipe *);
typedef RES(int, int) process_result_t;

OPTDEF(pipe_on_read_t);

typedef struct _read_pipe {
    int                pipe[2];
    int                fd;
    opt_pipe_on_read_t on_read;
    sb_t               buffers[2];
    int                current_buffer;
    sb_t               text;
} read_pipe_t;

typedef struct _read_pipes {
    pthread_mutex_t mutex;
    pthread_cond_t  condition;
    read_pipe_t     pipes[2];
} read_pipes_t;

typedef struct _write_pipe {
    int pipe[2];
    int fd;
} write_pipe_t;

typedef struct _process {
    pid_t              pid;
    slice_t            command;
    slices_t           arguments;
    bool               verbose;
    write_pipe_t       in;
    read_pipes_t       out_pipes;
    opt_pipe_on_read_t on_stdout_read;
    opt_pipe_on_read_t on_stderr_read;
} process_t;

int              read_pipe_initialize(read_pipe_t *pipe);
int              read_pipe_connect(read_pipe_t *pipe, int fd);
void             read_pipe_connect_parent(read_pipe_t *);
void             read_pipe_connect_child(read_pipe_t *, int fd);
void             read_pipe_close(read_pipe_t *);
int              read_pipes_initialize(read_pipes_t *p);
void            *read_pipes_read(void *p);
int              read_pipes_drain(read_pipes_t *p, channel_t channel);
bool             read_pipes_expect(read_pipes_t *p, channel_t channel);
slice_t          read_pipes_current(read_pipes_t *p, channel_t channel);
void             read_pipes_connect_parent(read_pipes_t *p);
void             read_pipes_connect_child(read_pipes_t *p, int out, int err);
void             read_pipes_close(read_pipes_t *p);
int              write_pipe_initialize(write_pipe_t *p);
void             write_pipe_close(write_pipe_t *p);
void             write_pipe_connect_parent(write_pipe_t *p);
void             write_pipe_connect_child(write_pipe_t *p, int fd);
ssize_t          write_pipe_write(write_pipe_t *p, slice_t sv);
ssize_t          write_pipe_write_chars(write_pipe_t *p, char const *buf, size_t num);
void             _process_set_arguments(process_t *proc, ...);
int              process_start(process_t *proc);
process_result_t process_wait(process_t *proc);
process_result_t process_execute(process_t *proc);
int              process_write(process_t *proc, slice_t sv);

#define process_set_arguments(p, ...) \
    _process_set_arguments((p), ##__VA_ARGS__, NULL)

#define process_create(cmd, ...)                               \
    (                                                          \
        {                                                      \
            process_t __p = {                                  \
                .command = C(cmd),                             \
            };                                                 \
            _process_set_arguments(&__p, ##__VA_ARGS__, NULL); \
            (__p);                                             \
        })

#define cmd_execute(cmd, ...)                                  \
    (                                                          \
        {                                                      \
            process_t __p = {                                  \
                .command = cmd,                                \
            };                                                 \
            _process_set_arguments(&__p, ##__VA_ARGS__, NULL); \
            (process_execute(&p));                             \
        })

#define process_background(p) process_start(p)

#endif /* __PROCESS_H__ */

#ifdef PROCESS_IMPLEMENTATION
#ifndef PROCESS_IMPLEMENTED

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#define PROCESS_PIPE_END_READ 0
#define PROCESS_PIPE_END_WRITE 1
#define PROCESS_DRAIN_SIZE (64 * 1024)

void sigchld(int sig)
{
    (void) sig;
}

int read_pipe_initialize(read_pipe_t *p)
{
    return pipe(p->pipe);
}

int read_pipe_connect(read_pipe_t *p, int fd)
{
    p->fd = fd;
    return fcntl(p->fd, F_SETFL, O_NONBLOCK);
}

void read_pipe_connect_parent(read_pipe_t *p)
{
    if (read_pipe_connect(p, p->pipe[PROCESS_PIPE_END_READ]) < 0) {
        fatal("read_pipe_connect: %s\n", strerror(errno));
    }
    close(p->pipe[PROCESS_PIPE_END_WRITE]);
}

void read_pipe_connect_child(read_pipe_t *p, int fd)
{
    while ((dup2(p->pipe[PROCESS_PIPE_END_WRITE], fd) == -1) && (errno == EINTR)) { }
    p->fd = fd;
    close(p->pipe[PROCESS_PIPE_END_READ]);
    close(p->pipe[PROCESS_PIPE_END_WRITE]);
}

void read_pipe_close(read_pipe_t *p)
{
    if (p->fd >= 0) {
        close(p->fd);
    }
    p->fd = -1;
}

int read_pipes_initialize(read_pipes_t *p)
{
    for (int ix = 0; ix < 2; ++ix) {
        int err = read_pipe_initialize(p->pipes + ix);
        if (err != 0) {
            return err;
        }
    }
    return 0;
}

void *read_pipes_read(void *arg)
{
    read_pipes_t *p = (read_pipes_t *) arg;
    struct pollfd poll_fd[2] = { 0 };
    for (int ix = 0; ix < 2; ++ix) {
        poll_fd[ix].fd = p->pipes[ix].fd;
        poll_fd[ix].events = POLLIN;
    }

    for (bool done = false; !done;) {
        if (poll(poll_fd, 2, -1) == -1) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (int ix = 0; ix < 2; ++ix) {
            if (poll_fd[ix].revents & POLLIN) {
                if (read_pipes_drain(p, ix) < 0) {
                    fatal("read_pipes_read: drain: %s\n", strerror(errno));
                }
            }
            if (poll_fd[ix].revents & POLLHUP) {
                done = true;
                break;
            }
        }
    }
    return p;
}

typedef void *(*threadproc_t)(void *);

void read_pipes_connect_parent(read_pipes_t *p)
{
    read_pipe_connect_parent(&p->pipes[PROCESS_PIPE_STDOUT]);
    read_pipe_connect_parent(p->pipes + PROCESS_PIPE_STDERR);

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if ((errno = pthread_mutex_init(&p->mutex, &attr)) != 0) {
        fatal("read_pipes_connect_parent: pthread_mutex_init: %s\n", strerror(errno));
    }
    pthread_t thread;
    pthread_mutexattr_destroy(&attr);
    if ((errno = pthread_create(
             &thread,
             NULL,
             (threadproc_t) read_pipes_read, p))
        != 0) {
        fatal("read_pipes_connect_parent: pthread_create: %s\n", strerror(errno));
    }
    pthread_detach(thread);
}

void read_pipes_connect_child(read_pipes_t *p, int out, int err)
{
    read_pipe_connect_child(p->pipes + PROCESS_PIPE_STDOUT, out);
    read_pipe_connect_child(p->pipes + PROCESS_PIPE_STDERR, err);
}

void read_pipes_close(read_pipes_t *p)
{
    pthread_cond_broadcast(&p->condition);
    read_pipe_close(p->pipes + PROCESS_PIPE_STDOUT);
    read_pipe_close(p->pipes + PROCESS_PIPE_STDERR);
}

bool read_pipes_expect(read_pipes_t *p, channel_t channel)
{
    read_pipe_t *pip = p->pipes + channel;
    pthread_mutex_lock(&p->mutex);
    do {
        if (pthread_cond_wait(&p->condition, &p->mutex) != 0) {
            pthread_mutex_unlock(&p->mutex);
            return false;
        }
    } while (pip->buffers[pip->current_buffer].len == 0 && pip->fd >= 0);
    pthread_mutex_unlock(&p->mutex);
    return pip->fd >= 0;
}

slice_t read_pipes_current(read_pipes_t *p, channel_t channel)
{
    read_pipe_t *pip = p->pipes + channel;
    if (pip->fd < 0) {
        return sb_as_slice(pip->buffers[pip->current_buffer]);
    }

    pthread_mutex_lock(&p->mutex);
    while (pip->fd >= 0 && pip->buffers[pip->current_buffer].len == 0) {
        pthread_cond_wait(&p->condition, &p->mutex);
    }
    slice_t ret = sb_as_slice(pip->buffers[pip->current_buffer]);
    pip->current_buffer = 1 - pip->current_buffer;
    sb_clear(pip->buffers + pip->current_buffer);
    pthread_mutex_unlock(&p->mutex);
    return ret;
}

int read_pipes_drain(read_pipes_t *p, channel_t channel)
{
    read_pipe_t *pip = p->pipes + channel;
    if (pip->fd < 0) {
        return 0;
    }

    pthread_mutex_lock(&p->mutex);
    char   buffer[PROCESS_DRAIN_SIZE];
    size_t offset = pip->buffers[pip->current_buffer].len;
    while (true) {
        ssize_t count = read(pip->fd, buffer, sizeof(buffer) - 1);
        if (count >= 0) {
            buffer[count] = 0;
            if (count > 0) {
                sb_append_cstr(pip->buffers + pip->current_buffer, buffer);
                if (count == sizeof(buffer) - 1) {
                    continue;
                }
            }
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        pthread_mutex_unlock(&p->mutex);
        return -1;
    }
    if (pip->buffers[pip->current_buffer].len != offset && pip->on_read.ok) {
        (pip->on_read.value)(pip);
    } else {
        sb_append(&pip->text, slice_tail(sb_as_slice(pip->buffers[pip->current_buffer]), offset));
    }
    pthread_cond_broadcast(&p->condition);
    pthread_mutex_unlock(&p->mutex);
    return 0;
}

int write_pipe_initialize(write_pipe_t *p)
{
    return pipe(p->pipe);
}

void write_pipe_close(write_pipe_t *p)
{
    if (p->fd >= 0) {
        close(p->fd);
        p->fd = -1;
    }
}

void write_pipe_connect_parent(write_pipe_t *p)
{
    p->fd = p->pipe[PROCESS_PIPE_END_WRITE];
    close(p->pipe[PROCESS_PIPE_END_READ]);
}

void write_pipe_connect_child(write_pipe_t *p, int fd)
{
    while ((dup2(p->pipe[PROCESS_PIPE_END_READ], fd) == -1) && (errno == EINTR)) { }
    close(p->pipe[PROCESS_PIPE_END_READ]);
    close(p->pipe[PROCESS_PIPE_END_WRITE]);
}

ssize_t write_pipe_write(write_pipe_t *p, slice_t sv)
{
    return write_pipe_write_chars(p, sv.items, sv.len);
}

ssize_t write_pipe_write_chars(write_pipe_t *p, char const *buf, size_t num)
{
    ssize_t total = 0;
    while (total < (ssize_t) num) {
        ssize_t count = write(p->fd, buf + total, num - total);
        if (count < 0) {
            if (errno != EINTR) {
                return count;
            }
            continue;
        }
        total += count;
    }
    return total;
}

void _process_set_arguments(process_t *proc, ...)
{
    va_list args;
    va_start(args, proc);
    for (char *arg = va_arg(args, char *); arg != NULL; arg = va_arg(args, char *)) {
        dynarr_append(&proc->arguments, slice_make(arg, strlen(arg)));
    }
    va_end(args);
}

int process_start(process_t *proc)
{
    if (proc->verbose) {
        printf("[CMD] " SL, SLARG(proc->command));
        for (size_t ix = 0; ix < proc->arguments.len; ++ix) {
            printf(" " SL, SLARG(proc->arguments.items[ix]));
        }
        printf("\n");
    }
    signal(SIGCHLD, sigchld);
    size_t sz = proc->arguments.len;
    size_t bufsz = proc->command.len + 1;
    for (size_t ix = 0; ix < sz; ++ix) {
        bufsz += proc->arguments.items[ix].len + 1;
    }
    char        buf[bufsz];
    char const *argv[sz + 2];
    strncpy(buf, proc->command.items, proc->command.len);
    buf[proc->command.len] = 0;
    argv[0] = buf;
    char *bufptr = buf + proc->command.len + 1;
    for (size_t ix = 0u; ix < sz; ++ix) {
        slice_t arg = proc->arguments.items[ix];
        strncpy(bufptr, arg.items, arg.len);
        bufptr[arg.len] = 0;
        argv[ix + 1] = bufptr;
        bufptr = bufptr + arg.len + 1;
    }
    argv[sz + 1] = NULL;

    // signal(SIGCHLD, SIG_IGN);
    int err;
    if ((err = write_pipe_initialize(&proc->in)) != 0) {
        return err;
    }
    proc->out_pipes.pipes[PROCESS_PIPE_STDOUT].on_read = proc->on_stdout_read;
    proc->out_pipes.pipes[PROCESS_PIPE_STDERR].on_read = proc->on_stderr_read;
    if ((err = read_pipes_initialize(&proc->out_pipes)) != 0) {
        return err;
    }

    proc->pid = fork();
    if (proc->pid == -1) {
        return -1;
    }
    if (proc->pid == 0) {
        write_pipe_connect_child(&proc->in, STDIN_FILENO);
        read_pipes_connect_child(&proc->out_pipes, STDOUT_FILENO, STDERR_FILENO);
        assert(argv[0] != NULL);
        execvp(argv[0], (char *const *) argv);
        fatal("execvp(" SL ") failed: %s\n", SLARG(proc->command), strerror(errno));
    }
    write_pipe_connect_parent(&proc->in);
    read_pipes_connect_parent(&proc->out_pipes);
    return 0;
}

process_result_t process_wait(process_t *proc)
{
    if (proc->pid == 0) {
        return RESVAL(process_result_t, 0);
    }
    int exit_code;
    if (waitpid(proc->pid, &exit_code, 0) == -1 && errno != ECHILD && errno != EINTR) {
        return RESERR(process_result_t, errno);
    }
    proc->pid = 0;
    write_pipe_close(&proc->in);
    if (!WIFEXITED(exit_code)) {
        return RESERR(process_result_t, exit_code);
    }
    return RESVAL(process_result_t, WEXITSTATUS(exit_code));
}

process_result_t process_execute(process_t *proc)
{
    if (process_start(proc) < 0) {
        return RESERR(process_result_t, errno);
    }
    return process_wait(proc);
}

int process_write(process_t *proc, slice_t sv)
{
    return write_pipe_write(&proc->in, sv);
}

#endif /* PROCESS_IMPLEMENTED */
#endif /* PROCESS_IMPLEMENTATION */

#ifdef PROCESS_TEST

#define HELLO_WORLD "Hello, World!"

bool dc_ok = false;

void dc_catch(read_pipe_t *p)
{
    dc_ok = slice_eq(C("2\n"), sb_as_slice(p->buffers[p->current_buffer]));
}

int main()
{
    {
        process_t proc = {
            .command = C("echo"),
        };
        process_set_arguments(&proc, HELLO_WORLD);
        process_execute(&proc);
        assert(slice_eq(C(HELLO_WORLD "\n"), sb_as_slice(proc.out_pipes.pipes[0].text)));
    }
    {
        process_t proc = {
            .command = C("dc"),
            .on_stdout_read = OPTVAL(pipe_on_read_t, dc_catch),
        };
        process_start(&proc);
        process_write(&proc, C("1\n"));
        process_write(&proc, C("1\n+\np\n"));
        process_write(&proc, C("q\n"));
        process_wait(&proc);
        assert(dc_ok);
    }
    return 0;
}

#endif /* PROCESS_TEST */
