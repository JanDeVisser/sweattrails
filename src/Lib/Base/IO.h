/*
 * Copyright (c) 2023, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "Error.h"
#include <expected>
#include <fstream>
#include <netinet/in.h>
#include <string>

#include <Expected.h>
#include <Utf8.h>

namespace ST {

using socket_t = std::shared_ptr<struct Socket>;

struct Socket {
    int         fd { 0 };
    std::string buffer { };

    std::expected<socket_t, LibCError>    accept();
    std::expected<void, LibCError>        make_nonblocking();
    std::expected<std::string, LibCError> read(size_t count);
    std::expected<std::string, LibCError> readln();
    std::expected<size_t, LibCError>      write(std::string_view const &buf, size_t num);
    std::expected<size_t, LibCError>      writeln(std::string_view const &buf);
    CError                                close();

    Socket(int fd);
    static std::expected<socket_t, LibCError> listen(std::string_view const &unix_socket_name);
    static std::expected<socket_t, LibCError> listen(std::string_view const &ip_address, int port);
    static std::expected<socket_t, LibCError> connect(std::string_view const &unix_socket_name);
    static std::expected<socket_t, LibCError> connect(std::string_view const &ip_address, int port);

private:
    std::expected<size_t, LibCError> read_available_bytes();
    std::expected<size_t, LibCError> fill_buffer();
};

std::expected<struct sockaddr_in, LibCError> tcpip_address_resolve(std::string_view const &ip_address);
CError                                       fd_make_nonblocking(int fd);

template<typename T = char>
std::expected<std::basic_string<T>, LibCError> read_file_by_name(std::string_view const &file_name)
{
    std::ifstream is(std::string { file_name });
    if (!is) {
        return std::unexpected(LibCError());
    }
    std::string ret;
    for (char ch; is.get(ch);) {
        ret += ch;
    }
    return ret;
}

template<>
inline std::expected<std::wstring, LibCError> read_file_by_name(std::string_view const &file_name)
{
    std::ifstream is(std::string { file_name });
    if (!is) {
        return std::unexpected(LibCError());
    }
    return read_utf8(is);
}

template<typename T = char>
std::expected<ssize_t, LibCError> write_file_by_name(std::string_view const &file_name, std::basic_string_view<T> const &contents)
{
    std::basic_fstream<T> os(std::string { file_name });
    if (!os) {
        return std::unexpected(LibCError());
    }
    os.write(contents.data(), contents.length());
    if (os.fail() || os.bad()) {
        return std::unexpected(LibCError());
    }
    return contents.length();
}

template<>
inline std::expected<ssize_t, LibCError> write_file_by_name(std::string_view const &file_name, std::wstring_view const &contents)
{
    std::ofstream os(std::string { file_name });
    if (!os) {
        return std::unexpected(LibCError());
    }
    return write_utf8(os, contents);
}

template<typename T = char>
std::expected<ssize_t, LibCError> write_file_by_name(std::string_view const &file_name, std::basic_string<T> const &contents)
{
    return write_file_by_name(file_name, std::basic_string_view<T> { contents });
}

}
