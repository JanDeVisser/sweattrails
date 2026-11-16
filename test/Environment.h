/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Setting environment variables, and the time zone, in a test, on POSIX
 * and Windows alike.
 */

#pragma once

#include <cstdlib>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>

namespace ST::Test {

inline void set_environment(char const *name, char const *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    ::setenv(name, value, 1);
#endif
}

inline void unset_environment(char const *name)
{
#ifdef _WIN32
    // An empty value removes it.
    _putenv_s(name, "");
#else
    ::unsetenv(name);
#endif
}

// TZ, to give back to restore_time_zone().
inline std::optional<std::string> saved_time_zone()
{
    auto const *tz = std::getenv("TZ");
    return tz ? std::optional<std::string> { tz } : std::nullopt;
}

inline void restore_time_zone(std::optional<std::string> const &saved)
{
    if (saved) {
        set_environment("TZ", saved->c_str());
    } else {
        unset_environment("TZ");
    }
    tzset();
}

// Puts the test in time zone `zone`, such as "America/Toronto". Windows's C
// runtime knows no such names, only TZ values such as "EST5EDT", always with
// the US's summer time rules: there only UTC and US Eastern time can be had,
// and other zones return false, for the test to skip what needs them.
inline bool set_time_zone(std::string_view zone)
{
#ifdef _WIN32
    char const *tz { nullptr };
    if (zone == "UTC") {
        tz = "UTC0";
    } else if (zone == "America/Toronto") {
        tz = "EST5EDT";
    } else {
        return false;
    }
    set_environment("TZ", tz);
#else
    set_environment("TZ", std::string { zone }.c_str());
#endif
    tzset();
    return true;
}

}
