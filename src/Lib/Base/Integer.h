/*
 * Copyright (c) 2023, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <concepts>
#include <cstdint>
#include <format>
#include <optional>

#include <LibCore/Logging.h>

namespace LibCore {

#define INTEGER_SIZES(S) S(8) S(16) S(32) S(64)

class IntegerType {
public:
    IntegerType(IntegerType const &) = default;
#undef S
#define S(sz)                                                       \
    static IntegerType I##sz()                                      \
    {                                                               \
        return IntegerType(sz, true, INT##sz##_MIN, INT##sz##_MAX); \
    }                                                               \
    static IntegerType U##sz()                                      \
    {                                                               \
        return IntegerType(sz, false, 0, UINT##sz##_MAX);           \
    }
    INTEGER_SIZES(S)
#undef S
    [[nodiscard]] uint8_t  size() const { return m_size; }
    [[nodiscard]] bool     is_signed() const { return m_is_signed; }
    [[nodiscard]] bool     is_unsigned() const { return !m_is_signed; }
    [[nodiscard]] int64_t  min() const { return m_min; }
    [[nodiscard]] uint64_t max() const { return m_max; }

    [[nodiscard]] std::string name() const
    {
        return std::format("{}{}", (m_is_signed) ? 'i' : 'u', m_size);
    }

    static std::optional<IntegerType> from_name(std::string_view const &name)
    {
#undef S
#define S(sz)              \
    if (name == "i" #sz) { \
        return I##sz();    \
    }                      \
    if (name == "u" #sz) { \
        return U##sz();    \
    }
        INTEGER_SIZES(S)
#undef S
        return {};
    }

    static IntegerType by_size(uint8_t size, bool is_signed)
    {
        if (is_signed) {
            switch (size) {
#undef S
#define S(sz) \
    case sz:  \
        return I##sz();
                INTEGER_SIZES(S)
#undef S
            default:
                UNREACHABLE();
            }
        } else {
            switch (size) {
#undef S
#define S(sz) \
    case sz:  \
        return U##sz();
                INTEGER_SIZES(S)
#undef S
            default:
                UNREACHABLE();
            }
        }
    }

    static IntegerType coerce(IntegerType t1, IntegerType t2)
    {
        return by_size(std::max(t1.size(), t2.size()), t1.is_signed() || t2.is_signed());
    }

    bool operator==(IntegerType const &other) const = default;

private:
    IntegerType(uint8_t size, bool is_signed, int64_t min, uint64_t max)
        : m_size(size)
        , m_is_signed(is_signed)
        , m_min(min)
        , m_max(max)
    {
    }

    uint8_t  m_size;
    bool     m_is_signed;
    int64_t  m_min;
    uint64_t m_max;
};

struct Integer {
    IntegerType type;
    union {
#undef S
#define S(sz)           \
    int##sz##_t  i##sz; \
    uint##sz##_t u##sz;
        INTEGER_SIZES(S)
#undef S
    };

    explicit Integer(IntegerType type, uint64_t value = 0) // FIXME bounds checks
        : type(type)
    {
        switch (type.size()) {
#undef S
#define S(sz)                             \
    case sz:                              \
        assert(value <= type.max());      \
        if (type.is_signed()) {           \
            i##sz = (int##sz##_t) value;  \
        } else {                          \
            u##sz = (uint##sz##_t) value; \
        }                                 \
        break;
            INTEGER_SIZES(S)
#undef S
        default:
            UNREACHABLE();
        }
    }

    explicit Integer(IntegerType type, int64_t value) // FIXME bounds checks
        : type(type)
    {
        switch (type.size()) {
#undef S
#define S(sz)                                   \
    case sz:                                    \
        assert((uint64_t) value <= type.max()); \
        assert(value >= type.min());            \
        if (type.is_signed()) {                 \
            i##sz = (int##sz##_t) value;        \
        } else {                                \
            u##sz = (uint##sz##_t) value;       \
        }                                       \
        break;
            INTEGER_SIZES(S)
#undef S
        default:
            UNREACHABLE();
        }
    }

    template<typename Int>
    explicit Integer(Int)
        : type(IntegerType::U64())
        , u64(0)
    {
        UNREACHABLE();
    }

    template<>
    explicit Integer(int8_t value)
        : type(IntegerType::I8())
        , i8(value)
    {
    }

    template<>
    explicit Integer(uint8_t value)
        : type(IntegerType::U8())
        , u8(value)
    {
    }

    template<>
    explicit Integer(int16_t value)
        : type(IntegerType::I16())
        , i16(value)
    {
    }

    template<>
    explicit Integer(uint16_t value)
        : type(IntegerType::U16())
        , u16(value)
    {
    }

    template<>
    explicit Integer(int32_t value)
        : type(IntegerType::I32())
        , i32(value)
    {
    }

    template<>
    explicit Integer(uint32_t value)
        : type(IntegerType::U32())
        , u32(value)
    {
    }

    template<>
    explicit Integer(int64_t value)
        : type(IntegerType::I64())
        , i64(value)
    {
    }

    template<>
    explicit Integer(uint64_t value)
        : type(IntegerType::U64())
        , u64(value)
    {
    }

    template<std::integral Int=int64_t>
    [[nodiscard]] std::optional<Int> safe_value() const
    {
        UNREACHABLE();
    }

    template<std::signed_integral Int>
    [[nodiscard]] std::optional<Int> safe_value() const
    {
        IntegerType t = IntegerType::by_size(sizeof(Int) * 8, true);
        if (type.is_signed()) {
            auto v64 = safe_value<int64_t>();
            if (!v64.has_value() || v64.value() < t.min() || v64.value() > t.max()) {
                return {};
            }
            return (Int) v64;
        } else {
            auto v64 = safe_value<uint64_t>();
            if (!v64.has_value() || v64.value() > t.max()) {
                return {};
            }
            return (Int) v64;
        }
    }

    template<std::unsigned_integral Int>
    [[nodiscard]] std::optional<Int> safe_value() const
    {
        IntegerType t = IntegerType::by_size(sizeof(Int) * 8, false);
        if (type.is_signed()) {
            auto v64 = safe_value<int64_t>();
            if (!v64.has_value() || v64.value() < 0 || v64.value() > t.max()) {
                return {};
            }
            return (Int) v64;
        } else {
            auto v64 = safe_value<uint64_t>();
            if (!v64.has_value() || v64.value() > t.max()) {
                return {};
            }
            return (Int) v64;
        }
    }

    template<>
    [[nodiscard]] std::optional<int64_t> safe_value() const
    {
        switch (type.size()) {
        case 8:
            return (type.is_signed()) ? i8 : u8;
        case 16:
            return (type.is_signed()) ? i16 : u16;
        case 32:
            return (type.is_signed()) ? i32 : u32;
        case 64:
            if (type.is_unsigned() && u64 > INT64_MAX) {
                return {};
            }
            return (type.is_signed()) ? i64 : u64;
        default:
            UNREACHABLE();
        }
    }

    template<>
    [[nodiscard]] std::optional<uint64_t> safe_value() const
    {
        switch (type.size()) {
        case 8:
            if (type.is_signed() && i8 < 0) {
                return {};
            }
            return (type.is_signed()) ? i8 : u8;
        case 16:
            if (type.is_signed() && i16 < 0) {
                return {};
            }
            return (type.is_signed()) ? i16 : u16;
        case 32:
            if (type.is_signed() && i32 < 0) {
                return {};
            }
            return (type.is_signed()) ? i32 : u32;
        case 64:
            if (type.is_signed() && i64 < 0) {
                return {};
            }
            return (type.is_signed()) ? i64 : u64;
        default:
            UNREACHABLE();
        }
    }

    template<typename Int>
    [[nodiscard]] Int value() const
    {
        switch (type.size()) {
#undef S
#define S(sz)                               \
    case sz:                                \
        if (type.is_signed()) {             \
            return static_cast<Int>(i##sz); \
        }                                   \
        return static_cast<Int>(u##sz);
            INTEGER_SIZES(S)
#undef S
        default:
            UNREACHABLE();
        }
    }

    [[nodiscard]] std::optional<Integer> coerce_to_signed(IntegerType to_type) const
    {
        assert(to_type.is_signed());
        auto v = safe_value();
        if (type.is_signed()) {
            assert(v.has_value());
        } else if (!v.has_value()) {
            return {};
        }
        if (v.value() < to_type.min() || v.value() > to_type.max()) {
            return {};
        }
        return Integer(to_type, v.value());
    }

    [[nodiscard]] std::optional<Integer> coerce_to_unsigned(IntegerType to_type) const
    {
        assert(to_type.is_unsigned());
        std::optional<uint64_t> v = safe_value<uint64_t>();
        if (type.is_unsigned()) {
            assert(v.has_value());
        } else if (!v.has_value()) {
            return {};
        }
        if (v.value() > to_type.max()) {
            return {};
        }
        return Integer(to_type, v.value());
    }

    [[nodiscard]] std::optional<Integer> coerce_to(IntegerType to_type) const
    {
        if (to_type == type || (type.size() > to_type.size() && type.is_signed() == to_type.is_signed())) {
            return *this;
        }
        if (to_type.is_signed()) {
            return coerce_to_signed(to_type);
        }
        return coerce_to_unsigned(to_type);
    }

    template<typename Callback>
    [[nodiscard]] Integer bin_op(Integer i2, Callback op) const
    {
        IntegerType t = IntegerType::coerce(type, i2.type);
        auto        i1_maybe = coerce_to(t);
        assert(i1_maybe.has_value());
        Integer i1 = i1_maybe.value();
        auto    i2_maybe = coerce_to(t);
        assert(i2_maybe.has_value());
        i2 = i2_maybe.value();
        if (t.is_signed()) {
            auto v1 = i1.value<int64_t>();
            auto v2 = i2.value<int64_t>();
            auto result = op(v1, v2);
            assert(result >= t.min() && result <= t.max());
            return Integer(t, result);
        } else {
            assert(i1.is_unsigned());
            auto v1 = i1.value<uint64_t>();
            auto v2 = i2.value<uint64_t>();
            auto result = op(v1, v2);
            assert(result >= t.min() && result <= t.max());
            return Integer(t, result);
        }
    }

    template<typename Callback>
    [[nodiscard]] bool bool_op(Integer i2, Callback op) const
    {
        IntegerType t = IntegerType::coerce(type, i2.type);
        auto        i1_maybe = coerce_to(t);
        assert(i1_maybe.has_value());
        Integer i1 = i1_maybe.value();
        auto    i2_maybe = coerce_to(t);
        assert(i2_maybe.has_value());
        i2 = i2_maybe.value();
        if (t.is_signed()) {
            auto v1 = i1.value<int64_t>();
            auto v2 = i2.value<int64_t>();
            return op(v1, v2);
        } else {
            assert(i1.is_unsigned());
            auto v1 = i1.value<uint64_t>();
            auto v2 = i2.value<uint64_t>();
            return op(v1, v2);
        }
    }

    [[nodiscard]] Integer operator+(Integer const &i2) const
    {
        return bin_op(i2, [](int64_t v1, int64_t v2) -> int64_t { return v1 + v2; });
    }

    [[nodiscard]] Integer operator-(Integer const &i2) const
    {
        return bin_op(i2, [](int64_t v1, int64_t v2) -> int64_t { return v1 - v2; });
    }

    [[nodiscard]] Integer operator*(Integer const &i2) const
    {
        return bin_op(i2, [](int64_t v1, int64_t v2) -> int64_t { return v1 * v2; });
    }

    [[nodiscard]] Integer operator/(Integer const &i2) const
    {
        return bin_op(i2, [](int64_t v1, int64_t v2) -> int64_t { return v1 / v2; });
    }

    [[nodiscard]] Integer operator%(Integer const &i2) const
    {
        return bin_op(i2, [](auto v1, auto v2) -> decltype(v1) { return v1 % v2; });
    }

    [[nodiscard]] bool operator==(Integer const &i2) const
    {
        return bool_op(i2, [](auto v1, auto v2) -> bool { return v1 == v2; });
    }

    [[nodiscard]] bool operator!=(Integer const &i2) const
    {
        return bool_op(i2, [](auto v1, auto v2) -> bool { return v1 != v2; });
    }

    [[nodiscard]] bool operator<(Integer const &i2) const
    {
        return bool_op(i2, [](auto v1, auto v2) -> bool { return v1 < v2; });
    }

    [[nodiscard]] bool operator<=(Integer const &i2) const
    {
        return bool_op(i2, [](auto v1, auto v2) -> bool { return v1 <= v2; });
    }

    [[nodiscard]] bool operator>(Integer const &i2) const
    {
        return bool_op(i2, [](auto v1, auto v2) -> bool { return v1 < v2; });
    }

    [[nodiscard]] bool operator>=(Integer const &i2) const
    {
        return bool_op(i2, [](auto v1, auto v2) -> bool { return v1 <= v2; });
    }

    [[nodiscard]] Integer operator&(Integer const &i2) const
    {
        return bin_op(i2, [](auto v1, auto v2) -> decltype(v1) { return v1 & v2; });
    }

    [[nodiscard]] Integer operator|(Integer const &i2) const
    {
        return bin_op(i2, [](auto v1, auto v2) -> decltype(v1) { return v1 | v2; });
    }

    [[nodiscard]] Integer operator^(Integer const &i2) const
    {
        return bin_op(i2, [](auto v1, auto v2) -> decltype(v1) { return v1 ^ v2; });
    }

    [[nodiscard]] Integer operator<<(Integer const &i2) const
    {
        return bin_op(i2, [](auto v1, auto v2) -> decltype(v1) { return v1 << v2; });
    }

    [[nodiscard]] Integer operator>>(Integer const &i2) const
    {
        return bin_op(i2, [](auto v1, auto v2) -> decltype(v1) { return v1 >> v2; });
    }

    [[nodiscard]] Integer operator-() const
    {
        if (type.is_signed()) {
            auto val = (int64_t) * this;
            return Integer(type, -val);
        } else {
            IntegerType ret_type = IntegerType::by_size(type.size(), true);
            auto        val = (uint64_t) * this;
            assert(val <= ret_type.max());
            return Integer(ret_type, -((int64_t) val));
        }
    }

    [[nodiscard]] Integer operator~() const
    {
        return Integer(type, ~this->u64);
    }

    [[nodiscard]] Integer operator++() const
    {
        if (is_signed()) {
            auto v = (int64_t) * this;
            if (v < type.max()) {
                v += 1;
            } else {
                v = type.min();
            }
            return Integer(type, v);
        } else {
            auto v = (uint64_t) * this;
            if (v < type.max()) {
                v += 1;
            } else {
                v = 0;
            }
            return Integer(type, v + 1);
        }
    }

    [[nodiscard]] Integer operator--() const
    {
        if (is_signed()) {
            auto v = (int64_t) * this;
            if (v > type.min()) {
                v -= 1;
            } else {
                v = (int64_t) type.max();
            }
            return Integer(type, v);
        } else {
            auto v = (uint64_t) * this;
            if (v > 0) {
                v -= 1;
            } else {
                v = type.max();
            }
            return Integer(type, v);
        }
    }

    [[nodiscard]] bool is_signed() const
    {
        return type.is_signed();
    }

    [[nodiscard]] bool is_unsigned() const
    {
        return type.is_unsigned();
    }

#undef S
#define S(sz)                                        \
    static Integer I##sz(int64_t value)              \
    {                                                \
        return Integer(IntegerType::I##sz(), value); \
    }                                                \
    static Integer U##sz(uint64_t value)             \
    {                                                \
        return Integer(IntegerType::U##sz(), value); \
    }
    INTEGER_SIZES(S)
#undef S

private:
    [[nodiscard]] explicit operator uint64_t() const
    {
        assert(type.is_unsigned());
        return value<uint64_t>();
    }

    [[nodiscard]] explicit operator int64_t() const
    {
        assert(type.is_signed());
        return value<int64_t>();
    }
};

}
