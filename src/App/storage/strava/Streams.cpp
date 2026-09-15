/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/streams.zig.
 */

#include "Streams.h"

namespace ST::strava {

Decoded<TimeStream> TimeStream::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "TimeStream"));
    }
    TimeStream ret { };
    ST_STRAVA_FIELDS_TimeStream(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue TimeStream::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_TimeStream(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<DistanceStream> DistanceStream::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "DistanceStream"));
    }
    DistanceStream ret { };
    ST_STRAVA_FIELDS_DistanceStream(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue DistanceStream::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_DistanceStream(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<LatLngStream> LatLngStream::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "LatLngStream"));
    }
    LatLngStream ret { };
    ST_STRAVA_FIELDS_LatLngStream(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue LatLngStream::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_LatLngStream(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<AltitudeStream> AltitudeStream::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "AltitudeStream"));
    }
    AltitudeStream ret { };
    ST_STRAVA_FIELDS_AltitudeStream(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue AltitudeStream::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_AltitudeStream(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<SmoothVelocityStream> SmoothVelocityStream::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "SmoothVelocityStream"));
    }
    SmoothVelocityStream ret { };
    ST_STRAVA_FIELDS_SmoothVelocityStream(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue SmoothVelocityStream::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_SmoothVelocityStream(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<HeartrateStream> HeartrateStream::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "HeartrateStream"));
    }
    HeartrateStream ret { };
    ST_STRAVA_FIELDS_HeartrateStream(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue HeartrateStream::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_HeartrateStream(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<CadenceStream> CadenceStream::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "CadenceStream"));
    }
    CadenceStream ret { };
    ST_STRAVA_FIELDS_CadenceStream(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue CadenceStream::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_CadenceStream(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<PowerStream> PowerStream::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "PowerStream"));
    }
    PowerStream ret { };
    ST_STRAVA_FIELDS_PowerStream(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue PowerStream::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_PowerStream(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<TemperatureStream> TemperatureStream::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "TemperatureStream"));
    }
    TemperatureStream ret { };
    ST_STRAVA_FIELDS_TemperatureStream(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue TemperatureStream::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_TemperatureStream(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<MovingStream> MovingStream::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "MovingStream"));
    }
    MovingStream ret { };
    ST_STRAVA_FIELDS_MovingStream(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue MovingStream::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_MovingStream(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<SmoothGradeStream> SmoothGradeStream::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "SmoothGradeStream"));
    }
    SmoothGradeStream ret { };
    ST_STRAVA_FIELDS_SmoothGradeStream(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue SmoothGradeStream::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_SmoothGradeStream(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<StreamSet> StreamSet::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "StreamSet"));
    }
    StreamSet ret { };
    ST_STRAVA_FIELDS_StreamSet(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue StreamSet::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_StreamSet(ST_STRAVA_FIELD_ENCODE) return obj;
}

}
