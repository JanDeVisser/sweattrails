/*
 * Copyright (c) 2021, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <Logging.h>
#include <StringBuffer.h>

namespace ST {

// logging_category(stringbuffer);

StringBuffer::StringBuffer(StringBuffer &other)
    : m_buffer_string(other.m_buffer_string)
    , m_char_buffer(other.m_char_buffer)
{
    other.m_buffer_string = { };
    other.m_char_buffer = { };
    if (m_buffer_string.has_value()) {
        m_buffer = m_buffer_string->c_str();
    } else if (m_char_buffer.has_value()) {
        m_buffer = m_char_buffer.value();
    } else {
        m_buffer = other.m_buffer;
    }
}

StringBuffer::StringBuffer(StringBuffer &&other) noexcept
    : m_buffer_string(std::move(other.m_buffer_string))
    , m_char_buffer(other.m_char_buffer)
{
    if (m_buffer_string.has_value()) {
        m_buffer = m_buffer_string->c_str();
    } else if (m_char_buffer.has_value()) {
        m_buffer = m_char_buffer.value();
    } else {
        m_buffer = other.m_buffer;
    }
    other.m_char_buffer = { };
    other.m_buffer_string = { };
}

StringBuffer::StringBuffer(std::string str)
    : m_buffer_string(std::move(str))
    , m_buffer(m_buffer_string.value().c_str())
{
}

StringBuffer::StringBuffer(std::string_view str)
    : m_buffer(str)
{
}

StringBuffer::StringBuffer(char const *str, bool take_ownership)
{
    if (take_ownership && str)
        m_char_buffer = str;
    m_buffer = (str) ? str : "";
}

StringBuffer::~StringBuffer()
{
    if (m_char_buffer.has_value())
        delete[] m_char_buffer.value();
}

StringBuffer &StringBuffer::assign(std::string buffer)
{
    if (m_char_buffer.has_value())
        delete[] m_char_buffer.value();
    m_char_buffer = { };
    m_buffer_string = std::move(buffer);
    m_buffer = m_buffer_string.value().c_str();
    m_pos = m_mark = 0;
    return *this;
}

StringBuffer &StringBuffer::assign(char const *buffer, bool take_ownership)
{
    if (buffer == nullptr) {
        assign(std::string(""));
        rewind();
        return *this;
    }
    if (take_ownership) {
        if (m_char_buffer.has_value()) {
            delete[] m_char_buffer.value();
            m_char_buffer = { };
        }
        m_char_buffer = buffer;
    }
    m_buffer_string = { };
    m_buffer = buffer;
    m_pos = m_mark = 0;
    return *this;
}

StringBuffer &StringBuffer::assign(StringBuffer buffer)
{
    if (m_char_buffer.has_value()) {
        delete[] m_char_buffer.value();
        m_char_buffer = { };
    }
    if (buffer.m_buffer_string.has_value()) {
        m_buffer_string = std::move(buffer.m_buffer_string.value());
        m_buffer = m_buffer_string.value().c_str();
    } else if (buffer.m_char_buffer.has_value()) {
        m_char_buffer = buffer.m_char_buffer;
        buffer.m_char_buffer = { };
        m_buffer = m_char_buffer.value();
    } else {
        m_buffer = buffer.m_buffer;
    }
    m_pos = m_mark = 0;
    return *this;
}

StringBuffer &StringBuffer::assign(std::string_view buffer)
{
    m_buffer_string = { };
    m_char_buffer = { };
    m_buffer = buffer;
    m_pos = m_mark = 0;
    return *this;
}

void StringBuffer::rewind()
{
    m_pos = m_mark;
}

void StringBuffer::reset()
{
    m_mark = m_pos;
}

void StringBuffer::partial_rewind(size_t num)
{
    if (num > (m_pos - m_mark))
        num = (m_pos - m_mark);
    m_pos -= num;
}

[[maybe_unused]] void StringBuffer::pushback()
{
    if (m_pos > m_mark)
        m_pos--;
}

std::string_view StringBuffer::read(size_t num)
{
    if (static_cast<int>(num) < 0)
        num = 0;
    if ((m_pos + num) > m_buffer.length())
        num = m_buffer.length() - m_pos;
    auto ret = m_buffer.substr(m_pos, num);
    m_pos = m_pos + num;
    return ret;
}

std::string_view StringBuffer::scanned_string() const
{
    return m_buffer.substr(m_mark, m_pos - m_mark);
}

int StringBuffer::peek(size_t num) const
{
    return ((m_pos + num) < m_buffer.length()) ? m_buffer[m_pos + num] : 0;
}

int StringBuffer::readchar()
{
    return (m_pos < m_buffer.length() - 1) ? m_buffer[++m_pos] : 0;
}

bool StringBuffer::top() const
{
    return m_pos == 0;
}

bool StringBuffer::eof() const
{
    return m_pos >= m_buffer.length();
}

void StringBuffer::skip(size_t num)
{
    if (m_pos + num > m_buffer.length()) {
        num = m_buffer.length() - m_pos;
    }
    m_pos += num;
}

bool StringBuffer::expect(char ch, size_t offset)
{
    if (peek(offset) != ch)
        return false;
    m_pos += offset + 1;
    return true;
}

bool StringBuffer::expect(std::string const &str, size_t offset)
{
    if (m_buffer.length() < m_pos + offset)
        return false;
    if (m_buffer.length() < m_pos + offset + str.length())
        return false;
    if (m_buffer.substr(m_pos + offset, str.length()) != str)
        return false;
    m_pos += offset + str.length();
    return true;
}

bool StringBuffer::is_one_of(std::string const &str, size_t offset) const
{
    return str.find_first_of(static_cast<char>(peek(offset))) != std::string::npos;
}

[[maybe_unused]] bool StringBuffer::expect_one_of(std::string const &str, size_t offset)
{
    if (is_one_of(str, offset)) {
        m_pos += offset + 1;
        return true;
    }
    return false;
}

int StringBuffer::one_of(std::string const &str)
{
    if (str.find_first_of((char) peek()) != std::string::npos) {
        return readchar();
    }
    return 0;
}

}
