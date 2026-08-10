/*
 * Copyright (c) 2023, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <fstream>
#include <netinet/in.h>
#include <string>

#include <Result.h>
#include <Utf8.h>

namespace ST {

using socket_t = std::shared_ptr<struct Socket>;

struct Socket {
    int         fd { 0 };
    std::string buffer { };

    Result<socket_t>    accept();
    CError              make_nonblocking();
    Result<std::string> read(size_t count);
    Result<std::string> readln();
    Result<size_t>      write(std::string_view const &buf, size_t num);
    Result<size_t>      writeln(std::string_view const &buf);
    CError              close();

    Socket(int fd);
    static Result<socket_t> listen(std::string_view const &unix_socket_name);
    static Result<socket_t> listen(std::string_view const &ip_address, int port);
    static Result<socket_t> connect(std::string_view const &unix_socket_name);
    static Result<socket_t> connect(std::string_view const &ip_address, int port);

private:
    Result<size_t> read_available_bytes();
    Result<size_t> fill_buffer();
};

Result<struct sockaddr_in> tcpip_address_resolve(std::string_view const &ip_address);
CError                     fd_make_nonblocking(int fd);

template<typename T = char>
Result<std::basic_string<T>> read_file_by_name(std::string_view const &file_name)
{
    std::ifstream is(std::string { file_name });
    if (!is) {
        return LibCError();
    }
    std::string ret;
    for (char ch; is.get(ch);) {
        ret += ch;
    }
    return ret;
}

template<>
inline Result<std::wstring> read_file_by_name(std::string_view const &file_name)
{
    std::ifstream is(std::string { file_name });
    if (!is) {
        return LibCError();
    }
    return read_utf8(is);
}

template<typename T = char>
Result<ssize_t> write_file_by_name(std::string_view const &file_name, std::basic_string_view<T> const &contents)
{
    std::basic_fstream<T> os(std::string { file_name });
    if (!os) {
        return LibCError();
    }
    os.write(contents.data(), contents.length());
    if (os.fail() || os.bad()) {
        return LibCError();
    }
    return contents.length();
}

template<>
inline Result<ssize_t> write_file_by_name(std::string_view const &file_name, std::wstring_view const &contents)
{
    std::ofstream os(std::string { file_name });
    if (!os) {
        return LibCError();
    }
    return write_utf8(os, contents);
}

template<typename T = char>
Result<ssize_t> write_file_by_name(std::string_view const &file_name, std::basic_string<T> const &contents)
{
    return write_file_by_name(file_name, std::basic_string_view<T> { contents });
}

}
