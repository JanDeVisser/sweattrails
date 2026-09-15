/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/club.zig.
 */

#include "Club.h"

namespace ST::strava {

Decoded<SummaryClub> SummaryClub::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "SummaryClub"));
    }
    SummaryClub ret { };
    ST_STRAVA_FIELDS_SummaryClub(ST_STRAVA_FIELD_DECODE) if (auto opt_private = json.get("private"); opt_private)
    {
        bool value_private = false;
        TRY(opt_private->convert(value_private));
        ret.is_private = value_private;
    }
    return ret;
}

JSONValue SummaryClub::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_SummaryClub(ST_STRAVA_FIELD_ENCODE)
        ST::set(obj, "private", is_private);
    return obj;
}

Decoded<DetailedClub> DetailedClub::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "DetailedClub"));
    }
    DetailedClub ret { };
    ST_STRAVA_FIELDS_DetailedClub(ST_STRAVA_FIELD_DECODE) if (auto opt_private = json.get("private"); opt_private)
    {
        bool value_private = false;
        TRY(opt_private->convert(value_private));
        ret.is_private = value_private;
    }
    return ret;
}

JSONValue DetailedClub::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_DetailedClub(ST_STRAVA_FIELD_ENCODE)
        ST::set(obj, "private", is_private);
    return obj;
}

Decoded<ClubActivity> ClubActivity::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "ClubActivity"));
    }
    ClubActivity ret { };
    ST_STRAVA_FIELDS_ClubActivity(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue ClubActivity::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_ClubActivity(ST_STRAVA_FIELD_ENCODE) return obj;
}

}
