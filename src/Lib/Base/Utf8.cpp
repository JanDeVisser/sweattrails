/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cerrno>

#include <Utf8.h>

namespace ST {

namespace {

bool is_continuation(unsigned char byte)
{
    return (byte & 0xC0) == 0x80;
}

}

bool is_code_point(char32_t ch)
{
    auto const is_surrogate = ch >= 0xD800 && ch <= 0xDFFF;
    return ch <= 0x10FFFF && !is_surrogate;
}

std::expected<std::string, LibCError> to_utf8(std::u32string_view const &s)
{
    std::string ret;
    ret.reserve(s.length());
    for (auto const ch : s) {
        if (!is_code_point(ch)) {
            return std::unexpected(LibCError(EILSEQ));
        }
        if (ch < 0x80) {
            ret += static_cast<char>(ch);
        } else if (ch < 0x800) {
            ret += static_cast<char>(0xC0 | (ch >> 6));
            ret += static_cast<char>(0x80 | (ch & 0x3F));
        } else if (ch < 0x10000) {
            ret += static_cast<char>(0xE0 | (ch >> 12));
            ret += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
            ret += static_cast<char>(0x80 | (ch & 0x3F));
        } else {
            ret += static_cast<char>(0xF0 | (ch >> 18));
            ret += static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
            ret += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
            ret += static_cast<char>(0x80 | (ch & 0x3F));
        }
    }
    return ret;
}

std::expected<std::u32string, LibCError> to_utf32(std::string_view const &s)
{
    std::u32string ret;
    ret.reserve(s.length());
    for (size_t ix = 0; ix < s.length();) {
        auto const lead = static_cast<unsigned char>(s[ix]);
        size_t     length { 0 };
        char32_t   ch { 0 };
        char32_t   smallest { 0 }; // Below this, the sequence is overlong
        if (lead < 0x80) {
            length = 1;
            ch = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            length = 2;
            ch = lead & 0x1F;
            smallest = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            ch = lead & 0x0F;
            smallest = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4;
            ch = lead & 0x07;
            smallest = 0x10000;
        } else {
            return std::unexpected(LibCError(EILSEQ));
        }
        if (ix + length > s.length()) {
            return std::unexpected(LibCError(EILSEQ));
        }
        for (size_t continuation = 1; continuation < length; ++continuation) {
            auto const byte = static_cast<unsigned char>(s[ix + continuation]);
            if (!is_continuation(byte)) {
                return std::unexpected(LibCError(EILSEQ));
            }
            ch = (ch << 6) | (byte & 0x3F);
        }
        if (ch < smallest || !is_code_point(ch)) {
            return std::unexpected(LibCError(EILSEQ));
        }
        ret += ch;
        ix += length;
    }
    return ret;
}

bool is_space(char32_t ch)
{
    switch (ch) {
    case U'\t':
    case U'\n':
    case U'\v':
    case U'\f':
    case U'\r':
    case U' ':
    case 0x0085:
    case 0x1680:
    case 0x2028:
    case 0x2029:
    case 0x205F:
    case 0x3000:
        return true;
    default:
        // U+2007, the figure space, doesn't break.
        return ch >= 0x2000 && ch <= 0x200A && ch != 0x2007;
    }
}

bool is_control(char32_t ch)
{
    return ch < 0x20 || (ch >= 0x7F && ch < 0xA0) || ch == 0x2028 || ch == 0x2029;
}

}
