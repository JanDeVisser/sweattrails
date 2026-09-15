#pragma once

#include <format>
#include <ostream>
#include <sstream>
#include <type_traits>

namespace ST {

template<typename Enum>
char const *value_to_string(Enum)
{
    static_assert(false, "Specialize me");
}

}

template<typename Enum>
    requires(std::is_enum_v<Enum>())
std::ostream &operator<<(std::ostream &os, Enum value)
{
    os << ST::value_to_string<Enum>(value);
}

template<typename T>
struct SimpleFormatter : public std::formatter<T, char> {
    template<class ParseContext>
    constexpr ParseContext::iterator parse(ParseContext &ctx)
    {
        auto it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            throw std::format_error(std::format("Invalid format args for {}", typeid(T).name()));
        }
        return it;
    }

    template<class FmtContext>
    typename FmtContext::iterator format(T const &value, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << value;
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};

template<typename Enum>
struct EnumFormatter : public SimpleFormatter<Enum> {
    template<class FmtContext>
    typename FmtContext::iterator format(Enum value, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << value;
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};
