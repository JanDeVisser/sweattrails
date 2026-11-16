/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cerrno>
#include <mutex>

// winsock2.h before windows.h, which would bring the old winsock.h.
#include <winsock2.h>

#include <Socket.h>
#include <Win32.h>

namespace ST {

void start_sockets()
{
    static std::once_flag started;
    std::call_once(started, []() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}

LibCError socket_error()
{
    switch (auto const code = WSAGetLastError()) {
    case WSAEADDRINUSE:
        return LibCError { EADDRINUSE };
    case WSAEINTR:
        return LibCError { EINTR };
    case WSAEWOULDBLOCK:
        return LibCError { EWOULDBLOCK };
    case WSAECONNRESET:
        return LibCError { ECONNRESET };
    case WSAEACCES:
        return LibCError { EACCES };
    default:
        return win32_error(static_cast<DWORD>(code));
    }
}

void close_socket(socket_t socket)
{
    if (socket != InvalidSocket) {
        closesocket(socket);
    }
}

void make_nonblocking(socket_t socket)
{
    u_long on = 1;
    ioctlsocket(socket, FIONBIO, &on);
    SetHandleInformation(reinterpret_cast<HANDLE>(socket), HANDLE_FLAG_INHERIT, 0);
}

void ignore_sigpipe(socket_t)
{
}

int poll_sockets(pollfd *sockets, size_t count, int timeout_ms)
{
    return WSAPoll(sockets, static_cast<ULONG>(count), timeout_ms);
}

}
