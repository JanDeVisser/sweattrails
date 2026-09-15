/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/activity.zig.
 */

#include "Activity.h"

namespace ST::strava {

Decoded<Lap> Lap::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "Lap"));
    }
    Lap ret { };
    ST_STRAVA_FIELDS_Lap(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue Lap::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_Lap(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<Split> Split::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "Split"));
    }
    Split ret { };
    ST_STRAVA_FIELDS_Split(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue Split::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_Split(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<SummaryActivity> SummaryActivity::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "SummaryActivity"));
    }
    SummaryActivity ret { };
    ST_STRAVA_FIELDS_SummaryActivity(ST_STRAVA_FIELD_DECODE) if (auto opt_private = json.get("private"); opt_private)
    {
        bool value_private = false;
        TRY(opt_private->convert(value_private));
        ret.is_private = value_private;
    }
    return ret;
}

JSONValue SummaryActivity::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_SummaryActivity(ST_STRAVA_FIELD_ENCODE)
        ST::set(obj, "private", is_private);
    return obj;
}

Decoded<DetailedActivity> DetailedActivity::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "DetailedActivity"));
    }
    DetailedActivity ret { };
    ST_STRAVA_FIELDS_DetailedActivity(ST_STRAVA_FIELD_DECODE) if (auto opt_private = json.get("private"); opt_private)
    {
        bool value_private = false;
        TRY(opt_private->convert(value_private));
        ret.is_private = value_private;
    }
    return ret;
}

JSONValue DetailedActivity::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_DetailedActivity(ST_STRAVA_FIELD_ENCODE)
        ST::set(obj, "private", is_private);
    return obj;
}

Decoded<UpdatableActivity> UpdatableActivity::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "UpdatableActivity"));
    }
    UpdatableActivity ret { };
    ST_STRAVA_FIELDS_UpdatableActivity(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue UpdatableActivity::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_UpdatableActivity(ST_STRAVA_FIELD_ENCODE) return obj;
}

}
