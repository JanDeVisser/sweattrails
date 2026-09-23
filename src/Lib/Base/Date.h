/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/date.zig.
 *
 * `date.date_time` becomes ST::DateTime and `date.Duration` becomes
 * ST::Duration. The comptime-type parameters of `Duration.init(T, ...)` and
 * `Duration.getXxx(T)` become template parameters.
 */

#pragma once

#include <cassert>
#include <cmath>
#include <concepts>
#include <ctime>
#include <expected>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <Error.h>
#include <Format.h>
#include <Logging.h>

namespace ST {

i64 now();

struct Duration {
    static constexpr u32 SECONDS_PER_MINUTE = 60; // Duh.
    static constexpr u32 SECONDS_PER_HOUR = 60 * SECONDS_PER_MINUTE;
    static constexpr u32 SECONDS_PER_DAY = 24 * SECONDS_PER_HOUR;

    u32     elapsed { 0 };
    u8      days { 0 };
    u8      hours { 0 };
    u8      minutes { 0 };
    u8      seconds { 0 };
    float32 fraction { 0.0 };

    Duration() = default;

    template<Number T>
    explicit Duration(T the_elapsed)
    {
        if constexpr (std::floating_point<T>) {
            assert(the_elapsed > 0);
            fraction = static_cast<float32>(the_elapsed - std::trunc(the_elapsed));
            elapsed = static_cast<u32>(std::trunc(the_elapsed));
        } else {
            if constexpr (sizeof(T) > sizeof(u32)) {
                assert(the_elapsed < 0xFFFFFFFF);
            }
            if constexpr (std::is_signed_v<T>) {
                assert(the_elapsed > 0);
            }
            elapsed = static_cast<u32>(the_elapsed);
        }
        days = static_cast<u8>(elapsed / SECONDS_PER_DAY);
        hours = static_cast<u8>((elapsed % SECONDS_PER_DAY) / SECONDS_PER_HOUR);
        minutes = static_cast<u8>((elapsed % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE);
        seconds = static_cast<u8>(elapsed % SECONDS_PER_MINUTE);
    }

    template<Number T>
    [[nodiscard]] T get_days() const
    {
        if constexpr (std::floating_point<T>) {
            return static_cast<T>(days)
                + static_cast<T>(elapsed % SECONDS_PER_DAY) / SECONDS_PER_DAY
                + static_cast<T>(fraction) / SECONDS_PER_DAY;
        } else {
            return static_cast<T>(days);
        }
    }

    template<Number T>
    [[nodiscard]] T get_hours() const
    {
        auto const hrs = days * 24 + hours;
        if constexpr (std::floating_point<T>) {
            return static_cast<T>(hrs)
                + static_cast<T>(elapsed % SECONDS_PER_HOUR) / SECONDS_PER_HOUR
                + static_cast<T>(fraction) / SECONDS_PER_HOUR;
        } else {
            return static_cast<T>(hrs);
        }
    }

    template<Number T>
    [[nodiscard]] T get_minutes() const
    {
        // Note: this mirrors the zig code, which adds days, hours and minutes
        // instead of converting them.
        auto const mins = days * 24 + hours + minutes;
        if constexpr (std::floating_point<T>) {
            return static_cast<T>(mins)
                + static_cast<T>(elapsed % SECONDS_PER_MINUTE) / SECONDS_PER_MINUTE
                + static_cast<T>(fraction) / SECONDS_PER_MINUTE;
        } else {
            return static_cast<T>(mins);
        }
    }

    template<Number T>
    [[nodiscard]] T get_seconds() const
    {
        if constexpr (std::floating_point<T>) {
            return static_cast<T>(elapsed) + static_cast<T>(fraction);
        } else {
            return static_cast<T>(elapsed);
        }
    }

    bool operator==(Duration const &) const = default;
};

struct DateTime {
    static constexpr i32 FIT_TIMESTAMP_OFFSET = 631065600;

    enum class Month : u8 {
        January = 0,
        February,
        March,
        April,
        May,
        June,
        July,
        August,
        September,
        October,
        November,
        December,
    };

    enum class WeekDay : u8 {
        Sunday = 0,
        Monday,
        Tuesday,
        Wednesday,
        Thursday,
        Friday,
        Saturday,
    };

    u32     timestamp { 0 };
    i16     year { 1970 };
    Month   month { Month::January };
    u8      day_of_month { 1 };
    WeekDay day_of_week { WeekDay::Sunday };
    u16     day_of_year { 0 };
    u8      hour { 0 };
    u8      minute { 0 };
    u8      second { 0 };

    static std::expected<DateTime, LibCError> parse_iso8601(std::string_view const &s);

    // date_time.init(?i32): converts a Unix timestamp to local time.
    static DateTime                from_timestamp(i32 timestamp);
    static std::optional<DateTime> from_timestamp(std::optional<i32> timestamp);

    [[nodiscard]] DateTime    end(Duration const &elapsed) const;
    [[nodiscard]] bool        less_than(DateTime const &rhs) const;
    [[nodiscard]] std::string format() const;

    bool operator==(DateTime const &) const = default;
};

// Named `month_name`/`weekday_name` rather than `to_string` because ST::to_string
// is a template struct in StringUtil.h.
std::string_view month_name(DateTime::Month month);
std::string_view weekday_name(DateTime::WeekDay day);

}

template<>
struct std::formatter<ST::DateTime> : std::formatter<std::string> {
    template<class FmtContext>
    FmtContext::iterator format(ST::DateTime const &val, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << val.format();
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};

template<>
struct std::formatter<ST::Duration> : std::formatter<std::string> {
    template<class FmtContext>
    FmtContext::iterator format(ST::Duration const &val, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << std::format("{:02}:{:02}:{:02}.{:03}",
            val.hours, val.minutes, val.seconds, static_cast<int>(val.fraction * 1000));
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};
