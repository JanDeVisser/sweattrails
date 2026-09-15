/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/common.zig.
 */

#include "Common.h"

namespace ST::strava {

Decoded<LatLng> LatLng::decode(JSONValue const &json)
{
    if (!json.is_array()) {
        return std::unexpected(JSONError::expected(JSONType::Array, json.type(), "LatLng"));
    }
    auto const arr = *json.to_array();
    if (arr.empty()) {
        // Strava returns `[]` for unknown positions.
        return LatLng { };
    }
    if (arr.size() != 2) {
        return std::unexpected(JSONError { JSONError::Code::TypeMismatch, "Expected a 2-element [lat, lon] array for LatLng" });
    }
    LatLng ret { };
    TRY(arr[0].convert(ret.lat));
    TRY(arr[1].convert(ret.lon));
    return ret;
}

JSONValue LatLng::encode() const
{
    auto ret = JSONValue::array();
    ret.append(JSONValue { lat });
    ret.append(JSONValue { lon });
    return ret;
}

Decoded<MetaActivity> MetaActivity::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "MetaActivity"));
    }
    MetaActivity ret { };
    ST_STRAVA_FIELDS_MetaActivity(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue MetaActivity::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_MetaActivity(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<MetaAthlete> MetaAthlete::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "MetaAthlete"));
    }
    MetaAthlete ret { };
    ST_STRAVA_FIELDS_MetaAthlete(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue MetaAthlete::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_MetaAthlete(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<MetaClub> MetaClub::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "MetaClub"));
    }
    MetaClub ret { };
    ST_STRAVA_FIELDS_MetaClub(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue MetaClub::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_MetaClub(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<PolylineMap> PolylineMap::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "PolylineMap"));
    }
    PolylineMap ret { };
    ST_STRAVA_FIELDS_PolylineMap(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue PolylineMap::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_PolylineMap(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<PhotosSummaryPrimary> PhotosSummaryPrimary::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "PhotosSummaryPrimary"));
    }
    PhotosSummaryPrimary ret { };
    ST_STRAVA_FIELDS_PhotosSummaryPrimary(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue PhotosSummaryPrimary::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_PhotosSummaryPrimary(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<PhotosSummary> PhotosSummary::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "PhotosSummary"));
    }
    PhotosSummary ret { };
    ST_STRAVA_FIELDS_PhotosSummary(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue PhotosSummary::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_PhotosSummary(ST_STRAVA_FIELD_ENCODE) return obj;
}

}
