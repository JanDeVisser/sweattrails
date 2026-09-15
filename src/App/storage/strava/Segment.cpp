/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/segment.zig.
 */

#include "Segment.h"

namespace ST::strava {

Decoded<SummaryPRSegmentEffort> SummaryPRSegmentEffort::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "SummaryPRSegmentEffort"));
    }
    SummaryPRSegmentEffort ret { };
    ST_STRAVA_FIELDS_SummaryPRSegmentEffort(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue SummaryPRSegmentEffort::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_SummaryPRSegmentEffort(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<SummarySegmentEffort> SummarySegmentEffort::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "SummarySegmentEffort"));
    }
    SummarySegmentEffort ret { };
    ST_STRAVA_FIELDS_SummarySegmentEffort(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue SummarySegmentEffort::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_SummarySegmentEffort(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<SummarySegment> SummarySegment::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "SummarySegment"));
    }
    SummarySegment ret { };
    ST_STRAVA_FIELDS_SummarySegment(ST_STRAVA_FIELD_DECODE) if (auto opt_private = json.get("private"); opt_private)
    {
        bool value_private = false;
        TRY(opt_private->convert(value_private));
        ret.is_private = value_private;
    }
    return ret;
}

JSONValue SummarySegment::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_SummarySegment(ST_STRAVA_FIELD_ENCODE)
        ST::set(obj, "private", is_private);
    return obj;
}

Decoded<DetailedSegmentEffort> DetailedSegmentEffort::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "DetailedSegmentEffort"));
    }
    DetailedSegmentEffort ret { };
    ST_STRAVA_FIELDS_DetailedSegmentEffort(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue DetailedSegmentEffort::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_DetailedSegmentEffort(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<DetailedSegment> DetailedSegment::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "DetailedSegment"));
    }
    DetailedSegment ret { };
    ST_STRAVA_FIELDS_DetailedSegment(ST_STRAVA_FIELD_DECODE) if (auto opt_private = json.get("private"); opt_private)
    {
        bool value_private = false;
        TRY(opt_private->convert(value_private));
        ret.is_private = value_private;
    }
    return ret;
}

JSONValue DetailedSegment::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_DetailedSegment(ST_STRAVA_FIELD_ENCODE)
        ST::set(obj, "private", is_private);
    return obj;
}

Decoded<ExplorerSegment> ExplorerSegment::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "ExplorerSegment"));
    }
    ExplorerSegment ret { };
    ST_STRAVA_FIELDS_ExplorerSegment(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue ExplorerSegment::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_ExplorerSegment(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<ExplorerResponse> ExplorerResponse::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "ExplorerResponse"));
    }
    ExplorerResponse ret { };
    ST_STRAVA_FIELDS_ExplorerResponse(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue ExplorerResponse::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_ExplorerResponse(ST_STRAVA_FIELD_ENCODE) return obj;
}

}
