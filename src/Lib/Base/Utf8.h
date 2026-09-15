/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <Expected.h>

namespace ST {

std::expected<std::string, LibCError>  to_utf8(std::wstring_view const &s);
std::expected<std::wstring, LibCError> to_wstring(std::string_view const &s);
std::expected<ssize_t, LibCError>      write_utf8(std::ofstream &os, std::wstring_view const &contents);
std::expected<std::wstring, LibCError> read_utf8(std::ifstream &is);

template<class T>
std::string as_utf8(std::basic_string_view<T> const &)
{
    UNREACHABLE();
}

template<>
inline std::string as_utf8(std::string_view const &s)
{
    return std::string { s };
}

template<>
inline std::string as_utf8(std::wstring_view const &s)
{
    return MUST_EVAL(to_utf8(s));
}

template<class T>
std::string as_utf8(std::basic_string<T> const &)
{
    UNREACHABLE();
}

template<>
inline std::string as_utf8(std::string const &s)
{
    return s;
}

template<>
inline std::string as_utf8(std::wstring const &s)
{
    return MUST_EVAL(to_utf8(s));
}

}
