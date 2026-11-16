/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Converting between UTF-8 and UTF-32. UTF-32 is std::u32string, not
 * std::wstring: wchar_t is 16 bits on Windows, which splits the characters
 * outside the BMP, such as emoji, in two.
 */

#pragma once

#include <string>
#include <string_view>

#include <Expected.h>

namespace ST {

// An error for text that isn't valid UTF-8, or UTF-32 with a code point
// that has no UTF-8: a surrogate, or one past U+10FFFF.
std::expected<std::string, LibCError>    to_utf8(std::u32string_view const &s);
std::expected<std::u32string, LibCError> to_utf32(std::string_view const &s);

// Whether `ch` is white space a line may break at: the Unicode White_Space
// characters, except the no-break spaces.
bool is_space(char32_t ch);

// Whether `ch` is a control character: C0, DEL, C1, and the line and
// paragraph separators.
bool is_control(char32_t ch);

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
inline std::string as_utf8(std::u32string_view const &s)
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
inline std::string as_utf8(std::u32string const &s)
{
    return MUST_EVAL(to_utf8(s));
}

}
