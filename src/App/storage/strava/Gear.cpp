/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/gear.zig.
 */

#include "Gear.h"

namespace ST::strava {

Decoded<SummaryGear> SummaryGear::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "SummaryGear"));
    }
    SummaryGear ret { };
    ST_STRAVA_FIELDS_SummaryGear(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue SummaryGear::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_SummaryGear(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<DetailedGear> DetailedGear::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "DetailedGear"));
    }
    DetailedGear ret { };
    ST_STRAVA_FIELDS_DetailedGear(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue DetailedGear::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_DetailedGear(ST_STRAVA_FIELD_ENCODE) return obj;
}

}
