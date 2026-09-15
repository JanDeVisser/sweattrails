/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cctype>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>

#include <Logging.h>

namespace ST {

class Scanner {
public:
    Scanner(std::string_view const &text = "")
        : m_text(text)
    {
    }

    Scanner &operator=(std::string_view const &text)
    {
        m_text = text;
        m_point.reset();
        m_mark.reset();
        return *this;
    }

    Scanner &append(std::string_view const &text)
    {
        m_text += text;
        return *this;
    }

    Scanner &operator+=(std::string_view const &text)
    {
        return append(text);
    }

    Scanner &rewind()
    {
        m_point = m_mark;
        return *this;
    }

    Scanner &reset()
    {
        m_mark = m_point;
        return *this;
    }

    Scanner &partial_rewind(size_t num)
    {
        num = std::min(num, m_point - m_mark);
        if (num > 0) {
            rewind();
            skip(m_point - m_mark - num);
        }
        return *this;
    }

    Scanner &pushback()
    {
        return partial_rewind(1);
    }

    std::string_view const read(size_t num)
    {
        auto ret = slice(m_point.index, num);
        skip(ret.size());
        return ret;
    }

    std::string_view const read_from_mark()
    {
        auto ret = slice(m_mark.index, m_point - m_mark);
        reset();
        return ret;
    }

    int readchar()
    {
        skip();
        return (m_point.index <= m_text.size() - 1) ? m_text[m_point.index] : 0;
    }

    int peek(size_t offset = 0) const
    {
        return ((m_point.index + offset) < m_text.length()) ? m_text[m_point.index + offset] : 0;
    }

    std::string_view const peek_chars(size_t num) const
    {
        num = std::min(num, m_text.size() - m_point.index);
        if (num == 0) {
            return std::string_view { };
        }
        return slice(m_point.index, num);
    }

    template<std::integral Int>
    std::optional<Int> read_number(size_t radix = 10)
    {
        auto chars = 0u;
        while (isdigit(peek(chars))) {
            chars++;
        }
        if (chars > 0) {
            auto number_str = read(chars);
            reset();
            auto result = string_to_integer<Int>(number_str, radix);
            assert(result.has_value());
            return result;
        }
        return { };
    }

    std::string_view const peek_tail() const
    {
        return slice(m_point.index, m_text.size() - m_point.index);
    }

    Scanner &skip(size_t num = 1)
    {
        num = std::min(num, m_text.size() - m_point.index);
        for (size_t new_index = m_point.index + num; m_point.index < new_index; ++m_point.index) {
            if (m_text[m_point.index] == '\n') {
                ++m_point.line;
                m_point.column = 0;
            } else {
                ++m_point.column;
            }
        }
        return *this;
    }

    template<typename Predicate>
    Scanner &skip_until(Predicate const &predicate)
    {
        while (peek() && !predicate()) {
            skip();
        }
        return *this;
    }

    Scanner &skip_whitespace()
    {
        return skip_until([this]() { return !isspace(peek()); });
    }

    Scanner &skip_until_char(int ch)
    {
        return skip_until([this, ch]() { return peek() == ch; });
    }

    bool expect_char(char ch, size_t offset = 0)
    {
        if (peek(offset) != ch) {
            return false;
        }
        skip(offset + 1);
        return true;
    }

    bool expect_string(std::string_view const &s)
    {
        if (m_point.index + s.length() > m_text.length()) {
            return false;
        }
        auto head = slice(m_point.index, s.length());
        if (head != s) {
            return false;
        }
        skip(s.length());
        return true;
    }

    bool is_one_of(std::string_view const &expect, size_t offset = 0)
    {
        return expect.contains(peek(offset));
    }

    bool expect_one_of(std::string_view const &expect, size_t offset = 0)
    {
        if (is_one_of(expect, offset)) {
            skip(offset + 1);
            return true;
        }
        return false;
    }

    int read_one_of(std::string_view const &expect)
    {
        if (expect.contains(peek())) {
            return readchar();
        }
        return 0;
    }

    struct TextPosition {
        size_t index { 0 };
        size_t line { 0 };
        size_t column { 0 };

        size_t operator-(TextPosition const &other) const
        {
            assert(index >= other.index);
            return static_cast<ptrdiff_t>(index - other.index);
        }

        auto operator<=>(TextPosition const &other)
        {
            return index <=> other.index;
        }

        void reset()
        {
            index = line = column = 0;
        }
    };

    size_t index() const
    {
        return m_point.index;
    }

private:
    std::string_view const slice(size_t from, size_t num) const
    {
        if (from >= m_text.size()) {
            return std::string_view { };
        }
        num = std::min(num, m_text.size() - from);
        if (num == 0) {
            return std::string_view { };
        }
        auto it = m_text.begin() + from;
        return std::string_view { it, it + num };
    }

    std::string  m_text;
    TextPosition m_mark;
    TextPosition m_point;
};

}
