/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * What differs between BSD sockets and Winsock, for code that otherwise
 * uses the same calls on both (socket(), bind(), accept(), ...).
 * SocketPosix.cpp and SocketWin32.cpp implement it.
 *
 * This header includes no system headers: on Windows those bring in
 * windows.h, whose names clash with raylib's. A .cpp that makes socket
 * calls includes <sys/socket.h> and friends, or <winsock2.h> and
 * <ws2tcpip.h>, itself.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <Error.h>

struct pollfd;

namespace ST {

#ifdef _WIN32
using socket_t = std::uintptr_t; // Winsock's SOCKET
#else
using socket_t = int;
#endif

constexpr socket_t InvalidSocket = static_cast<socket_t>(-1);

// Gets sockets going: Winsock needs WSAStartup() before anything else. Can
// be called any number of times.
void start_sockets();

// The error of the last socket call that failed. Winsock's errors that
// have an errno value, such as WSAEADDRINUSE, get it.
LibCError socket_error();

// Closes `socket`, unless it's InvalidSocket.
void close_socket(socket_t socket);

// Makes `socket` non-blocking, and keeps it from the programs the app
// starts.
void make_nonblocking(socket_t socket);

// Writing to a connection the other side closed is an error, not a SIGPIPE
// that ends the app. Windows has no SIGPIPE.
void ignore_sigpipe(socket_t socket);

// poll(), or WSAPoll() on Windows. Returns the number of sockets ready, or
// -1 on an error, which socket_error() tells.
int poll_sockets(pollfd *sockets, size_t count, int timeout_ms);

}
