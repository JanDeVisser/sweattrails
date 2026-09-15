/*
 * Copyright (c) 2021, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cxxabi.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Logging.h>

namespace ST {

using StringList = std::vector<std::string>;
using WStringList = std::vector<std::wstring>;
using StringViewList = std::vector<std::string_view>;
using WStringViewList = std::vector<std::wstring_view>;

int isbdigit(int ch);

// int stricmp(const char*, const char*);
std::string  to_upper(std::string_view const &);
std::string  to_lower(std::string_view const &);
std::string  capitalize(std::string_view const &s);
std::wstring capitalize(std::wstring_view const &s);

std::size_t replace_all(std::string &, std::string_view, std::string_view);
StringList  split(std::string_view const &s, std::string_view const &sep);
StringList  split(std::string_view const &s, char sep);
StringList  split_by_whitespace(std::string_view const &s);
//
// std::string c_escape(std::string const& s);
// std::string join(StringList const& collection, char sep);
std::string_view strip(std::string_view const &s);
std::string_view rstrip(std::string_view const &s);
std::string_view lstrip(std::string_view const &s);
// std::vector<std::pair<std::string, std::string>> parse_pairs(std::string const& s, char pair_sep = ';', char name_value_sep = '=');
// std::string dequote(std::string const& s, char = '"');

inline bool string_compare_ic(std::string_view const &s1, std::string_view const &s2)
{
    return std::ranges::equal(s1, s2, [](unsigned char ch1, unsigned char ch2) { return std::tolower(ch1) == std::tolower(ch2); });
}

template<typename ElementType, typename ToString>
inline std::string join(std::vector<ElementType> const &collection, std::string_view const &sep, ToString const &tostring)
{
    std::string ret;
    auto        first = true;
    for (auto &elem : collection) {
        if (!first) {
            ret += sep;
        }
        ret += tostring(elem);
        first = false;
    }
    return ret;
}

inline std::string join(StringList const &collection, std::string_view const &sep)
{
    return join(collection, sep, [](std::string const &s) { return s; });
}

template<typename ElementType, typename ToString>
inline std::string join(std::vector<ElementType> const &collection, char sep, ToString const &tostring)
{
    std::string_view s { &sep, 1 };
    return join(collection, s, tostring);
}

inline std::string join(StringList const &collection, char sep)
{
    std::string_view s { &sep, 1 };
    return join(collection, s);
}

template<std::integral Int>
[[nodiscard]] inline std::string integer_to_string(Int integer, int radix = 10, char grouping_char = '\0')
{
    thread_local static char buf[128];
    char                    *ptr = &buf[127];
    *ptr = 0;

    int ix = 0;

    typedef typename std::make_unsigned<Int>::type UInt;
    UInt                                           integer_pos = integer;
    if (std::is_signed_v<Int> && (integer < 0))
        integer_pos = -integer;

    do {
        if (grouping_char && (++ix % 3) == 0)
            *(--ptr) = grouping_char;
        *(--ptr) = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[integer_pos % radix];
        integer_pos /= radix;
    } while (integer_pos > 0);
    if (std::is_signed_v<Int> && (integer < 0)) {
        *(--ptr) = '-';
    }
    return { ptr };
}

template<std::integral Int, typename Str = std::string>
[[nodiscard]] inline std::optional<Int> string_to_integer(Str const &s, int radix = 0)
{
    int start;
    int sign = 1;
    for (start = 0; isspace(s[start]) && start < s.length(); ++start)
        ;
    auto ch = s[start];
    if (ch == '-') {
        if (std::is_unsigned_v<Int>) {
            return { };
        }
        sign = -1;
        ++start;
    }

    switch (ch) {
    case '0': {
        if (start < s.length() - 2) {
            switch (toupper(s[start + 1])) {
            case 'X': {
                if (radix != 0 && radix != 16)
                    return { };
                radix = 16;
                start += 2;
            } break;
            case 'B': {
                if (radix != 0 && radix != 2)
                    return { };
                radix = 16;
                start += 2;
            } break;
            default:
                break;
            }
        }
    } break;
    case '$': {
        if (radix != 0 && radix != 16)
            return { };
        radix = 16;
        ++start;
    } break;
    default:
        break;
    }

    if (radix == 0)
        radix = 10;
    assert(radix < 36);

    Int      val { 0 };
    uint64_t exp { 1 };
    auto     trailing_ws = true;
    for (int ix = s.length() - 1; ix >= start; --ix) {
        ch = s[ix];
        if (trailing_ws && isspace(ch))
            continue;
        trailing_ws = false;
        if (std::is_signed_v<Int> && ch == '-') {
            if ((ix > start) || (exp == 1))
                return { };
            val = -val;
            continue;
        }
        char digit = (isdigit(ch)) ? (ch - '0') : ((toupper(ch) - 'A') + 10);
        if (digit < 0 || digit >= radix)
            return { };
        val += digit * exp;
        exp *= radix;
    }
    return val * sign;
}

template<std::integral Int, typename Str = std::string>
[[nodiscard]] inline Int must_string_to_integer(Str const &s, int radix = 0)
{
    auto ret = string_to_integer<Int>(s, radix);
    if (ret.has_value()) {
        return ret.value();
    }
    fatal("Could not convert '{}' to integer", s);
}

// -- to_string -------------------------------------------------------------

template<typename T>
struct to_string {
    std::string operator()(T const &value)
    {
        return value.to_string();
    }
};

template<typename T>
struct to_string<T *> {
    std::string operator()(T const *value)
    {
        return to_string<T>()(*value);
    }
};

template<>
struct to_string<std::string> {
    std::string operator()(std::string const &value)
    {
        return value;
    }
};

template<>
struct to_string<std::string_view> {
    std::string operator()(std::string_view const &value)
    {
        return static_cast<std::string>(value);
    }
};

template<>
struct to_string<char *> {
    std::string operator()(char const *value)
    {
        return { value };
    }
};

template<>
struct to_string<void *> {
    std::string operator()(void const *value)
    {
        return integer_to_string(reinterpret_cast<uintptr_t>(value));
    }
};

template<int N>
struct to_string<char[N]> {
    std::string operator()(char const value[N])
    {
        return { value };
    }
};

template<>
struct to_string<char> {
    std::string operator()(char value)
    {
        return { value };
    }
};

template<std::integral T>
struct to_string<T> {
    std::string operator()(T value)
    {
        return integer_to_string(value);
    }
};

template<std::unsigned_integral T>
struct to_hex_string {
    std::string operator()(T value)
    {
        return integer_to_string(value, 16);
    }
};

template<std::floating_point Float>
struct to_string<Float> {
    std::string operator()(Float value)
    {
        return std::to_string(value);
    }
};

template<>
struct to_string<bool> {
    std::string operator()(bool value)
    {
        return (value) ? "true" : "false";
    }
};

template<typename T>
struct to_string<std::shared_ptr<T>> {
    std::string operator()(std::shared_ptr<T> value)
    {
        if (value == nullptr)
            return "(null)";
        return to_string<T>()(*value);
    }
};

template<typename T>
struct to_string<std::unique_ptr<T>> {
    std::string operator()(std::unique_ptr<T> value)
    {
        if (value == nullptr)
            return "(null)";
        return to_string<T>(*value);
    }
};

template<typename T>
struct to_string<std::vector<T>> {
    std::string operator()(std::vector<T> const &value)
    {
        return join(value, ", ", [](T elem) { return to_string<T>()(elem); });
    }
};

template<>
struct to_string<std::type_info> {
    std::string operator()(std::type_info const &value)
    {
        int         status;
        auto        realname = abi::__cxa_demangle(value.name(), 0, 0, &status);
        std::string ret { value.name() };
        if (!status)
            ret = realname;
        free(realname);
        return ret;
    }
};

// -- to_long ---------------------------------------------------------------

template<typename T>
struct try_to_long {
    std::optional<long> operator()(T value)
    {
        return string_to_integer<long>(to_string<T>()(value));
    }
};

template<>
struct try_to_long<std::string> {
    std::optional<long> operator()(std::string const &value)
    {
        return string_to_integer<long>(value);
    }
};

template<>
struct try_to_long<bool> {
    std::optional<long> operator()(bool value)
    {
        return (value) ? 1 : 0;
    }
};

template<std::integral Int>
struct try_to_long<Int> {
    std::optional<long> operator()(Int value)
    {
        return value;
    }
};

template<std::floating_point Float>
struct try_to_long<Float> {
    std::optional<long> operator()(Float value)
    {
        return value;
    }
};

template<typename T>
struct try_to_long<std::vector<T>> {
    std::optional<long> operator()(std::vector<T> value)
    {
        return value.size();
    }
};

template<typename T, std::signed_integral Int>
Int to_int(T const &value)
{
    auto ret = try_to_long<T>()(value);
    assert(ret.has_value());
    return ret.value();
}

template<typename T = std::string>
long to_long(T const &value)
{
    return to_int<T, long>(value);
}

// -- to_ulong --------------------------------------------------------------

template<typename T>
struct try_to_ulong {
    std::optional<unsigned long> operator()(T value)
    {
        auto l = try_to_long<T>()(value);
        if (!l.has_value() || (l.value() < 0))
            return { };
        return l.value();
    }
};

template<>
struct try_to_ulong<std::string> {
    std::optional<unsigned long> operator()(std::string const &value)
    {
        return string_to_integer<unsigned long>(value);
    }
};

template<std::unsigned_integral UInt>
struct try_to_ulong<UInt> {
    std::optional<unsigned long> operator()(UInt value)
    {
        return value;
    }
};

template<std::signed_integral SInt>
struct try_to_ulong<SInt> {
    std::optional<unsigned long> operator()(SInt value)
    {
        if (value < 0)
            return { };
        return value;
    }
};

template<std::floating_point Float>
struct try_to_ulong<Float> {
    std::optional<unsigned long> operator()(Float value)
    {
        if (value < 0)
            return { };
        return value;
    }
};

template<typename T, std::unsigned_integral Int>
Int to_uint(T const &value)
{
    auto ret = try_to_ulong<T>()(value);
    assert(ret.has_value());
    return ret.value();
}

template<typename T = std::string>
inline unsigned long to_ulong(T const &value)
{
    return to_uint<T, unsigned long>(value);
}

// -- to_double -------------------------------------------------------------

inline std::optional<double> string_to_double(std::string const &str)
{
    char *end;
    errno = 0;
    auto ret = strtod(str.c_str(), &end);
    if ((end != (str.c_str() + str.length())) || (errno != 0))
        return { };
    if (ret == 0) {
        for (auto &ch : str) {
            if ((ch != '.') && (ch != '0'))
                return { };
        }
    }
    return ret;
}

template<typename T>
struct try_to_double {
    std::optional<double> operator()(T value)
    {
        return string_to_double(to_string<T>()(value));
    }
};

template<>
struct try_to_double<std::string> {
    std::optional<double> operator()(std::string const &value)
    {
        return string_to_double(value);
    }
};

template<std::integral Int>
struct try_to_double<Int> {
    std::optional<double> operator()(Int value)
    {
        return value;
    }
};

template<std::floating_point Float>
struct try_to_double<Float> {
    std::optional<double> operator()(Float value)
    {
        return value;
    }
};

template<typename T = std::string>
inline double to_double(T value)
{
    auto ret = try_to_double<T>()(value);
    assert(ret.has_value());
    return ret.value();
}

// -- to_bool ---------------------------------------------------------------

template<typename T>
struct try_to_bool {
    std::optional<bool> operator()(T value)
    {
        if (auto l = try_to_long<T>()(value); l.has_value())
            return l.value();
        return { };
    }
};

template<>
struct try_to_bool<std::string> {
    std::optional<bool> operator()(std::string const &value)
    {
        if (string_compare_ic(value, "true") == 0)
            return true;
        if (string_compare_ic(value, "false") == 0)
            return false;
        auto ret = try_to_long<std::string>()(value);
        if (!ret.has_value())
            return { };
        return (ret.value() != 0);
    }
};

template<typename T = std::string>
inline bool to_bool(T const &value)
{
    auto ret = try_to_bool<T>()(value);
    assert(ret.has_value());
    return ret.value();
}

}
