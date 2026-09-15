/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/zones.zig.
 */

#pragma once

#include <vector>

#include <JSON.h>

#include "Enums.h"
#include "StravaJson.h"

namespace ST::strava {

#define ST_STRAVA_FIELDS_ZoneRange(F) \
    F(i32, min)                       \
    F(i32, max)

struct ZoneRange {
    ST_STRAVA_FIELDS_ZoneRange(ST_STRAVA_FIELD_MEMBER)

        static Decoded<ZoneRange> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_TimedZoneRange(F) \
    F(i32, min)                            \
    F(i32, max)                            \
    F(i32, time)

struct TimedZoneRange {
    ST_STRAVA_FIELDS_TimedZoneRange(ST_STRAVA_FIELD_MEMBER)

        static Decoded<TimedZoneRange> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_ActivityZone(F)                 \
    F(i32, score)                                        \
    F(std::vector<TimedZoneRange>, distribution_buckets) \
    F(ActivityZoneType, type)                            \
    F(bool, sensor_based)                                \
    F(i32, points)                                       \
    F(bool, custom_zones)                                \
    F(i32, max)

struct ActivityZone {
    ST_STRAVA_FIELDS_ActivityZone(ST_STRAVA_FIELD_MEMBER)

        static Decoded<ActivityZone> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_HeartRateZoneRanges(F) \
    F(bool, custom_zones)                       \
    F(std::vector<ZoneRange>, zones)

struct HeartRateZoneRanges {
    ST_STRAVA_FIELDS_HeartRateZoneRanges(ST_STRAVA_FIELD_MEMBER)

        static Decoded<HeartRateZoneRanges> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_PowerZoneRanges(F) \
    F(std::vector<ZoneRange>, zones)

struct PowerZoneRanges {
    ST_STRAVA_FIELDS_PowerZoneRanges(ST_STRAVA_FIELD_MEMBER)

        static Decoded<PowerZoneRanges> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_Zones(F)      \
    F(HeartRateZoneRanges, heart_rate) \
    F(PowerZoneRanges, power)

struct Zones {
    ST_STRAVA_FIELDS_Zones(ST_STRAVA_FIELD_MEMBER)

        static Decoded<Zones> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

}
