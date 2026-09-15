/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/stats.zig.
 */

#pragma once

#include <JSON.h>

#include "StravaJson.h"

namespace ST::strava {

#define ST_STRAVA_FIELDS_ActivityTotal(F) \
    F(i32, count)                         \
    F(float32, distance)                  \
    F(i32, moving_time)                   \
    F(i32, elapsed_time)                  \
    F(float32, elevation_gain)            \
    F(i32, achievement_count)

struct ActivityTotal {
    ST_STRAVA_FIELDS_ActivityTotal(ST_STRAVA_FIELD_MEMBER)

        static Decoded<ActivityTotal> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_ActivityStats(F)   \
    F(double, biggest_ride_distance)        \
    F(double, biggest_climb_elevation_gain) \
    F(ActivityTotal, recent_ride_totals)    \
    F(ActivityTotal, recent_run_totals)     \
    F(ActivityTotal, recent_swim_totals)    \
    F(ActivityTotal, ytd_ride_totals)       \
    F(ActivityTotal, ytd_run_totals)        \
    F(ActivityTotal, ytd_swim_totals)       \
    F(ActivityTotal, all_ride_totals)       \
    F(ActivityTotal, all_run_totals)        \
    F(ActivityTotal, all_swim_totals)

struct ActivityStats {
    ST_STRAVA_FIELDS_ActivityStats(ST_STRAVA_FIELD_MEMBER)

        static Decoded<ActivityStats> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

}
