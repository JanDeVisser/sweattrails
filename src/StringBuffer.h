/*
 * Copyright (c) 2021, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cerrno>
#include <fcntl.h>
#include <optional>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace ST {

class StringBuffer {
public:
    StringBuffer() = default;
    StringBuffer(StringBuffer &other);
    StringBuffer(StringBuffer &&other) noexcept;
    explicit StringBuffer(std::string);
    explicit StringBuffer(std::string_view);
    explicit StringBuffer(char const *, bool = false);
    virtual ~StringBuffer();
    [[nodiscard]] std::string             str() const { return std::string(m_buffer); }
    [[nodiscard]]                         operator std::string_view const() const { return m_buffer; }
    [[nodiscard]] std::string_view const &buffer() const { return m_buffer; }
    void                                  rewind();
    void                                  reset();
    void                                  partial_rewind(size_t);
    [[maybe_unused]] void                 pushback();
    [[nodiscard]] size_t                  scanned() const { return m_pos - m_mark; }
    [[nodiscard]] std::string_view        scanned_string() const;
    std::string_view                      read(size_t);
    [[nodiscard]] int                     peek(size_t = 0) const;
    int                                   one_of(std::string const &);
    bool                                  expect(char, size_t = 0);
    bool                                  expect(std::string const &, size_t = 0);
    [[nodiscard]] bool                    is_one_of(std::string const &, size_t = 0) const;
    [[maybe_unused]] bool                 expect_one_of(std::string const &, size_t = 0);
    int                                   readchar();
    void                                  skip(size_t = 1);
    [[nodiscard]] bool                    top() const;
    [[nodiscard]] bool                    eof() const;
    StringBuffer                         &assign(char const *buffer, bool take_ownership = false);
    StringBuffer                         &assign(std::string);
    StringBuffer                         &assign(StringBuffer);
    StringBuffer                         &assign(std::string_view);

private:
    std::optional<std::string>  m_buffer_string { };
    std::optional<char const *> m_char_buffer { };
    std::string_view            m_buffer;
    size_t                      m_pos { 0 };
    size_t                      m_mark { 0 };
};

}
