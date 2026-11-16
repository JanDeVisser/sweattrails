/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cerrno>
#include <csignal>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <Socket.h>

namespace ST {

void start_sockets()
{
}

LibCError socket_error()
{
    return LibCError { errno };
}

void close_socket(socket_t socket)
{
    if (socket != InvalidSocket) {
        ::close(socket);
    }
}

void make_nonblocking(socket_t socket)
{
    ::fcntl(socket, F_SETFL, ::fcntl(socket, F_GETFL) | O_NONBLOCK);
    ::fcntl(socket, F_SETFD, FD_CLOEXEC);
}

void ignore_sigpipe([[maybe_unused]] socket_t socket)
{
#ifdef SO_NOSIGPIPE
    int on = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif
}

int poll_sockets(pollfd *sockets, size_t count, int timeout_ms)
{
    return ::poll(sockets, static_cast<nfds_t>(count), timeout_ms);
}

}
