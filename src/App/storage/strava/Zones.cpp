/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/zones.zig.
 */

#include "Zones.h"

namespace ST::strava {

Decoded<ZoneRange> ZoneRange::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "ZoneRange"));
    }
    ZoneRange ret { };
    ST_STRAVA_FIELDS_ZoneRange(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue ZoneRange::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_ZoneRange(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<TimedZoneRange> TimedZoneRange::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "TimedZoneRange"));
    }
    TimedZoneRange ret { };
    ST_STRAVA_FIELDS_TimedZoneRange(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue TimedZoneRange::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_TimedZoneRange(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<ActivityZone> ActivityZone::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "ActivityZone"));
    }
    ActivityZone ret { };
    ST_STRAVA_FIELDS_ActivityZone(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue ActivityZone::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_ActivityZone(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<HeartRateZoneRanges> HeartRateZoneRanges::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "HeartRateZoneRanges"));
    }
    HeartRateZoneRanges ret { };
    ST_STRAVA_FIELDS_HeartRateZoneRanges(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue HeartRateZoneRanges::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_HeartRateZoneRanges(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<PowerZoneRanges> PowerZoneRanges::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "PowerZoneRanges"));
    }
    PowerZoneRanges ret { };
    ST_STRAVA_FIELDS_PowerZoneRanges(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue PowerZoneRanges::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_PowerZoneRanges(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<Zones> Zones::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "Zones"));
    }
    Zones ret { };
    ST_STRAVA_FIELDS_Zones(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue Zones::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_Zones(ST_STRAVA_FIELD_ENCODE) return obj;
}

}
