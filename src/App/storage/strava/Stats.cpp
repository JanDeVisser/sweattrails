/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/stats.zig.
 */

#include "Stats.h"

namespace ST::strava {

Decoded<ActivityTotal> ActivityTotal::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "ActivityTotal"));
    }
    ActivityTotal ret { };
    ST_STRAVA_FIELDS_ActivityTotal(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue ActivityTotal::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_ActivityTotal(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<ActivityStats> ActivityStats::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "ActivityStats"));
    }
    ActivityStats ret { };
    ST_STRAVA_FIELDS_ActivityStats(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue ActivityStats::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_ActivityStats(ST_STRAVA_FIELD_ENCODE) return obj;
}

}
