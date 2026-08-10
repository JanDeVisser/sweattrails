/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string_view>

namespace LibCore {

struct TextPosition {
    size_t index { 0 };
    size_t line { 0 };
    size_t column { 0 };
};

template<typename Char>
struct StringScanner {
    using CharType = Char;
    using StringView = std::basic_string_view<CharType>;

    std::basic_string_view<Char> string { "" };
    TextPosition                 point;
    TextPosition                 mark;

    StringScanner() = default;
    explicit StringScanner(StringView s)
        : string(s)
    {
    }

    void rewind()
    {
        point = mark;
    }

    void reset()
    {
        mark = point;
    }

    void partial_rewind(size_t num)
    {
        if (num > (point.index - mark.index)) {
            num = point.index - mark.index;
        }
        if (num > 0) {
            rewind();
            skip(point.index - mark.index - num);
        }
    }

    void pushback()
    {
        partial_rewind(1);
    }

    StringView read(size_t num = 1)
    {
        if (static_cast<int64_t>(num) < 0) {
            num = 0;
        }
        if ((point.index + num) > string.length()) {
            num = string.length() - point.index;
        }
        auto ret { string.substr(point.index, num) };
        skip(num);
        return ret;
    }

    StringView read_from_mark()
    {
        size_t num = point.index - mark.index;
        if (num > 0) {
            return string.substr(mark.index, point.index - mark.index);
        }
        return "";
    }

    CharType readchar()
    {
        skip();
        return (point.index <= string.length() - 1) ? string[point.index] : '\0';
    }

    CharType peek(size_t offset = 0)
    {
        return ((point.index + offset) < string.length()) ? string[point.index + offset] : 0;
    }

    StringView peek_sv(size_t len)
    {
        if (point.index + len > string.length()) {
            len = string.length() - point.index;
        }
        if (len == 0) {
            return "";
        }
        return string.substr(point.index, len);
    }

    StringView peek_tail()
    {
        return string.substr(point.index);
    }

    void skip(size_t num = 1)
    {
        if (point.index + num > string.length()) {
            num = string.length() - point.index;
        }
        for (size_t new_index = point.index + num; point.index < new_index; ++point.index) {
            if (string[point.index] == '\n') {
                ++point.line;
                point.column = 0;
            } else {
                ++point.column;
            }
        }
    }

    void skip_whitespace()
    {
        while (isspace(peek())) {
            skip();
        }
    }

    void skip_until(CharType ch)
    {
        while (peek() && peek() != ch) {
            skip();
        }
    }

    bool expect(CharType ch, size_t offset = 0)
    {
        if (peek(offset) != ch) {
            return false;
        }
        skip(offset + 1);
        return true;
    }

    bool expect(StringView sv)
    {
        if (point.index + sv.length() > string.length()) {
            return false;
        }
        if (string.substr(point.index, sv.length()) != sv) {
            return false;
        }
        skip(sv.length());
        return true;
    }

    bool is_one_of(StringView expect, size_t offset)
    {
        CharType p = peek(offset);
        return std::any_of(expect.begin(), expect.end(), [p](CharType ch) { return ch == p; });
    }

    bool expect_one_of(StringView expect, size_t offset)
    {
        if (is_one_of(expect, offset)) {
            skip(offset + 1);
            return true;
        }
        return false;
    }

    int one_of(StringView expect)
    {
        if (is_one_of(expect)) {
            return readchar();
        }
        return 0;
    }

    size_t read_number()
    {
        size_t ix = 0;
        while (isdigit(peek(ix))) {
            ix++;
        }
        if (ix > 0) {
            std::basic_string<CharType> num { read(ix) };
            reset();
            return std::stoull(num);
        }
        return 0;
    };
};

}
