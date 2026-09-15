/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/athlete.zig.
 */

#include "Athlete.h"

namespace ST::strava {

Decoded<SummaryAthlete> SummaryAthlete::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "SummaryAthlete"));
    }
    SummaryAthlete ret { };
    ST_STRAVA_FIELDS_SummaryAthlete(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue SummaryAthlete::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_SummaryAthlete(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<DetailedAthlete> DetailedAthlete::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "DetailedAthlete"));
    }
    DetailedAthlete ret { };
    ST_STRAVA_FIELDS_DetailedAthlete(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue DetailedAthlete::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_DetailedAthlete(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<ClubAthlete> ClubAthlete::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "ClubAthlete"));
    }
    ClubAthlete ret { };
    ST_STRAVA_FIELDS_ClubAthlete(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue ClubAthlete::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_ClubAthlete(ST_STRAVA_FIELD_ENCODE) return obj;
}

}
