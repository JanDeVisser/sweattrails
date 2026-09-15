/*
 * Copyright (c) 2023, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <concepts>

#include <Logging.h>

namespace ST {

template<std::integral T>
class Checked {
public:
    constexpr Checked() = default;

    explicit constexpr Checked(T value)
        : m_value(value)
    {
    }

    template<std::integral U>
    constexpr Checked(U value)
    {
        m_overflow = !is_within_range<T>(value);
        m_value = value;
    }

    constexpr Checked(Checked const &) = default;

    constexpr Checked(Checked &&other) noexcept
        : m_value(std::swap(other.m_value, 0))
        , m_overflow(std::swap(other.m_overflow, false))
    {
    }

    template<typename U>
    constexpr Checked &operator=(U value)
    {
        *this = Checked(value);
        return *this;
    }

    constexpr Checked &operator=(Checked const &other) = default;

    constexpr Checked &operator=(Checked &&other)
    {
        m_value = std::swap(other.m_value, 0);
        m_overflow = std::swap(other.m_overflow, false);
        return *this;
    }

    [[nodiscard]] constexpr bool has_overflow() const
    {
        return m_overflow;
    }

    constexpr bool operator!() const
    {
        assert(!m_overflow);
        return !m_value;
    }

    constexpr T value() const
    {
        assert(!m_overflow);
        return m_value;
    }

    constexpr T value_unchecked() const
    {
        return m_value;
    }

    constexpr void add(T other)
    {
        m_overflow |= __builtin_add_overflow(m_value, other, &m_value);
    }

    constexpr void sub(T other)
    {
        m_overflow |= __builtin_sub_overflow(m_value, other, &m_value);
    }

    constexpr void mul(T other)
    {
        m_overflow |= __builtin_mul_overflow(m_value, other, &m_value);
    }

    constexpr void div(T other)
    {
        if constexpr (std::is_signed_v<T>) {
            // Ensure that the resulting value won't be out of range, this can only happen when dividing by -1.
            if (other == -1 && m_value == std::numeric_limits<T>::min()) {
                m_overflow = true;
                return;
            }
        }
        if (other == 0) {
            m_overflow = true;
            return;
        }
        m_value /= other;
    }

    constexpr void mod(T other)
    {
        auto initial = m_value;
        div(other);
        m_value *= other;
        m_value = initial - m_value;
    }

    constexpr void saturating_sub(T other)
    {
        sub(other);
        // Depending on whether other was positive or negative, we have to saturate to min or max.
        if (m_overflow && other <= 0)
            m_value = std::numeric_limits<T>::max();
        else if (m_overflow)
            m_value = std::numeric_limits<T>::min();
        m_overflow = false;
    }

    constexpr void saturating_add(T other)
    {
        add(other);
        // Depending on whether other was positive or negative, we have to saturate to max or min.
        if (m_overflow && other >= 0)
            m_value = std::numeric_limits<T>::max();
        else if (m_overflow)
            m_value = std::numeric_limits<T>::min();
        m_overflow = false;
    }

    constexpr Checked &operator+=(Checked const &other)
    {
        m_overflow |= other.m_overflow;
        add(other.value());
        return *this;
    }

    constexpr Checked &operator+=(T other)
    {
        add(other);
        return *this;
    }

    constexpr Checked &operator-=(Checked const &other)
    {
        m_overflow |= other.m_overflow;
        sub(other.value());
        return *this;
    }

    constexpr Checked &operator-=(T other)
    {
        sub(other);
        return *this;
    }

    constexpr Checked &operator*=(Checked const &other)
    {
        m_overflow |= other.m_overflow;
        mul(other.value());
        return *this;
    }

    constexpr Checked &operator*=(T other)
    {
        mul(other);
        return *this;
    }

    constexpr Checked &operator/=(Checked const &other)
    {
        m_overflow |= other.m_overflow;
        div(other.value());
        return *this;
    }

    constexpr Checked &operator/=(T other)
    {
        div(other);
        return *this;
    }

    constexpr Checked &operator%=(Checked const &other)
    {
        m_overflow |= other.m_overflow;
        mod(other.value());
        return *this;
    }

    constexpr Checked &operator%=(T other)
    {
        mod(other);
        return *this;
    }

    constexpr Checked &operator++()
    {
        add(1);
        return *this;
    }

    constexpr Checked operator++(int)
    {
        Checked old { *this };
        add(1);
        return old;
    }

    constexpr Checked &operator--()
    {
        sub(1);
        return *this;
    }

    constexpr Checked operator--(int)
    {
        Checked old { *this };
        sub(1);
        return old;
    }

    template<typename U, typename V>
    [[nodiscard]] static constexpr bool addition_would_overflow(U u, V v)
    {
#if __has_builtin(__builtin_add_overflow_p)
        return __builtin_add_overflow_p(u, v, (T) 0);
#else
        Checked checked;
        checked = u;
        checked += v;
        return checked.has_overflow();
#endif
    }

    template<typename U, typename V>
    [[nodiscard]] static constexpr bool multiplication_would_overflow(U u, V v)
    {
#if __has_builtin(__builtin_mul_overflow_p)
        return __builtin_mul_overflow_p(u, v, (T) 0);
#else
        Checked checked;
        checked = u;
        checked *= v;
        return checked.has_overflow();
#endif
    }

    template<typename U, typename V, typename X>
    [[nodiscard]] static constexpr bool multiplication_would_overflow(U u, V v, X x)
    {
        Checked checked;
        checked = u;
        checked *= v;
        checked *= x;
        return checked.has_overflow();
    }

private:
    T    m_value { };
    bool m_overflow { false };
};

template<typename T>
constexpr Checked<T> operator+(Checked<T> const &a, Checked<T> const &b)
{
    Checked<T> c { a };
    c.add(b.value());
    return c;
}

template<typename T>
constexpr Checked<T> operator-(Checked<T> const &a, Checked<T> const &b)
{
    Checked<T> c { a };
    c.sub(b.value());
    return c;
}

template<typename T>
constexpr Checked<T> operator*(Checked<T> const &a, Checked<T> const &b)
{
    Checked<T> c { a };
    c.mul(b.value());
    return c;
}

template<typename T>
constexpr Checked<T> operator/(Checked<T> const &a, Checked<T> const &b)
{
    Checked<T> c { a };
    c.div(b.value());
    return c;
}

template<typename T>
constexpr Checked<T> operator%(Checked<T> const &a, Checked<T> const &b)
{
    Checked<T> c { a };
    c.mod(b.value());
    return c;
}

template<typename T>
constexpr bool operator<(Checked<T> const &a, T b)
{
    return a.value() < b;
}

template<typename T>
constexpr bool operator>(Checked<T> const &a, T b)
{
    return a.value() > b;
}

template<typename T>
constexpr bool operator>=(Checked<T> const &a, T b)
{
    return a.value() >= b;
}

template<typename T>
constexpr bool operator<=(Checked<T> const &a, T b)
{
    return a.value() <= b;
}

template<typename T>
constexpr bool operator==(Checked<T> const &a, T b)
{
    return a.value() == b;
}

template<typename T>
constexpr bool operator!=(Checked<T> const &a, T b)
{
    return a.value() != b;
}

template<typename T>
constexpr bool operator<(T a, Checked<T> const &b)
{
    return a < b.value();
}

template<typename T>
constexpr bool operator>(T a, Checked<T> const &b)
{
    return a > b.value();
}

template<typename T>
constexpr bool operator>=(T a, Checked<T> const &b)
{
    return a >= b.value();
}

template<typename T>
constexpr bool operator<=(T a, Checked<T> const &b)
{
    return a <= b.value();
}

template<typename T>
constexpr bool operator==(T a, Checked<T> const &b)
{
    return a == b.value();
}

template<typename T>
constexpr bool operator!=(T a, Checked<T> const &b)
{
    return a != b.value();
}

template<typename T>
constexpr Checked<T> make_checked(T value)
{
    return Checked<T>(value);
}

}
