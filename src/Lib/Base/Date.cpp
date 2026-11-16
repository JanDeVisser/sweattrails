/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <array>
#include <charconv>
#include <format>

#include <Date.h>
#include <Expected.h>

namespace ST {

namespace {

constexpr std::array<std::string_view, 12> s_month_names {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

constexpr std::array<std::string_view, 7> s_weekday_names {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

template<typename T>
std::expected<T, LibCError> parse_int(std::string_view const &s)
{
    T    value { 0 };
    auto res = std::from_chars(s.data(), s.data() + s.length(), value, 10);
    if (res.ec != std::errc { } || res.ptr != s.data() + s.length()) {
        return std::unexpected(LibCError { std::format("Could not parse `{}` as a number", s) });
    }
    return value;
}

}

std::string_view month_name(DateTime::Month month)
{
    return s_month_names[static_cast<size_t>(month)];
}

std::string_view weekday_name(DateTime::WeekDay day)
{
    return s_weekday_names[static_cast<size_t>(day)];
}

i64 now()
{
    return static_cast<i64>(time(nullptr));
}

std::tm utc_tm(i64 time)
{
    auto const t = static_cast<std::time_t>(time);
    std::tm    ret { };
#ifdef _WIN32
    gmtime_s(&ret, &t);
#else
    gmtime_r(&t, &ret);
#endif
    return ret;
}

std::tm local_tm(i64 time)
{
    auto const t = static_cast<std::time_t>(time);
    std::tm    ret { };
#ifdef _WIN32
    localtime_s(&ret, &t);
#else
    localtime_r(&t, &ret);
#endif
    return ret;
}

i32 utc_offset(i64 time)
{
#ifdef _WIN32
    // Windows's tm has no tm_gmtoff: the local time, read as UTC, is that
    // much later.
    return static_cast<i32>(utc_time(local_tm(time)) - time);
#else
    return static_cast<i32>(local_tm(time).tm_gmtoff);
#endif
}

i64 utc_time(std::tm tm)
{
#ifdef _WIN32
    return static_cast<i64>(_mkgmtime(&tm));
#else
    return static_cast<i64>(timegm(&tm));
#endif
}

std::expected<DateTime, LibCError> DateTime::parse_iso8601(std::string_view const &s)
{
    //           1
    // 0123456789012345
    // 20250905T140950Z

    //           1
    // 01234567890123456789
    // 2025-09-05T14:09:50Z
    // 2025-09-05T14:09:50.123Z: the fraction of a second is dropped.
    if (s.length() > 21 && s[19] == '.' && s.back() == 'Z'
        && std::ranges::all_of(s.substr(20, s.length() - 21), [](char c) { return c >= '0' && c <= '9'; })) {
        return parse_iso8601(std::format("{}Z", s.substr(0, 19)));
    }
    if (s.length() != 20 && s.length() != 16) {
        return std::unexpected(LibCError { std::format("`{}` is not an ISO8601 timestamp", s) });
    }
    bool const   compact = s.length() == 16;
    size_t const mon_off = compact ? 4 : 5;
    size_t const day_off = compact ? 6 : 8;
    size_t const hour_off = compact ? 9 : 11;
    size_t const min_off = compact ? 11 : 14;
    size_t const sec_off = compact ? 13 : 17;

    // The zig version checks s[13] and s[16] unconditionally, which is wrong
    // (and out of bounds) for the compact form; here the separators are only
    // checked for the form that has them.
    if (compact) {
        if (s[8] != 'T' || s[15] != 'Z') {
            return std::unexpected(LibCError { std::format("`{}` is not an ISO8601 timestamp", s) });
        }
    } else {
        if (s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[19] != 'Z') {
            return std::unexpected(LibCError { std::format("`{}` is not an ISO8601 timestamp", s) });
        }
    }

    auto const year = TRY_EVAL(parse_int<i16>(s.substr(0, 4)));
    auto const m = TRY_EVAL(parse_int<u8>(s.substr(mon_off, 2)));
    auto const month = static_cast<Month>(m - 1);
    auto const day = TRY_EVAL(parse_int<u8>(s.substr(day_off, 2)));
    auto const hour = TRY_EVAL(parse_int<u8>(s.substr(hour_off, 2)));
    auto const minute = TRY_EVAL(parse_int<u8>(s.substr(min_off, 2)));
    auto const second = TRY_EVAL(parse_int<u8>(s.substr(sec_off, 2)));

    struct tm tm { };
    tm.tm_year = year - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = 0;
    auto const timestamp = utc_time(tm);
    tm = utc_tm(timestamp); // For the day of the week and of the year
    return DateTime {
        .timestamp = static_cast<u32>(timestamp),
        .year = year,
        .month = month,
        .day_of_month = day,
        .day_of_week = static_cast<WeekDay>(tm.tm_wday),
        .day_of_year = static_cast<u16>(tm.tm_yday),
        .hour = hour,
        .minute = minute,
        .second = second,
    };
}

DateTime DateTime::from_timestamp(i32 timestamp)
{
    auto const tm = local_tm(timestamp);
    return DateTime {
        .timestamp = static_cast<u32>(timestamp),
        .year = static_cast<i16>(tm.tm_year + 1900),
        .month = static_cast<Month>(tm.tm_mon),
        .day_of_month = static_cast<u8>(tm.tm_mday),
        .day_of_week = static_cast<WeekDay>(tm.tm_wday),
        .day_of_year = static_cast<u16>(tm.tm_yday),
        .hour = static_cast<u8>(tm.tm_hour),
        .minute = static_cast<u8>(tm.tm_min),
        .second = static_cast<u8>(tm.tm_sec),
    };
}

std::optional<DateTime> DateTime::from_timestamp(std::optional<i32> timestamp)
{
    if (!timestamp) {
        return { };
    }
    return from_timestamp(*timestamp);
}

std::string clock_time(i64 time)
{
    auto const local = DateTime::from_timestamp(static_cast<i32>(time));
    return std::format("{:02}:{:02}", local.hour, local.minute);
}

DateTime DateTime::end(Duration const &elapsed) const
{
    return from_timestamp(static_cast<i32>(timestamp + elapsed.elapsed));
}

bool DateTime::less_than(DateTime const &rhs) const
{
    return timestamp < rhs.timestamp;
}

std::string DateTime::format() const
{
    return std::format(
        "{} {} {}, {:04} {}:{:02}",
        weekday_name(day_of_week),
        month_name(month),
        day_of_month,
        year,
        hour,
        minute);
}

}
