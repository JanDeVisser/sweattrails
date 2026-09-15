/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/streams.zig.
 */

#pragma once

#include <vector>

#include <JSON.h>

#include "Common.h"
#include "Enums.h"
#include "StravaJson.h"

namespace ST::strava {

#define ST_STRAVA_STREAM_HEADER_FIELDS(F) \
    F(i32, original_size)                 \
    F(StreamResolution, resolution)       \
    F(StreamSeriesType, series_type)

#define ST_STRAVA_FIELDS_TimeStream(F) \
    ST_STRAVA_STREAM_HEADER_FIELDS(F)  \
    F(std::vector<i32>, data)

struct TimeStream {
    ST_STRAVA_FIELDS_TimeStream(ST_STRAVA_FIELD_MEMBER)

        static Decoded<TimeStream> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_DistanceStream(F) \
    ST_STRAVA_STREAM_HEADER_FIELDS(F)      \
    F(std::vector<float32>, data)

struct DistanceStream {
    ST_STRAVA_FIELDS_DistanceStream(ST_STRAVA_FIELD_MEMBER)

        static Decoded<DistanceStream> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_LatLngStream(F) \
    ST_STRAVA_STREAM_HEADER_FIELDS(F)    \
    F(std::vector<LatLng>, data)

struct LatLngStream {
    ST_STRAVA_FIELDS_LatLngStream(ST_STRAVA_FIELD_MEMBER)

        static Decoded<LatLngStream> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_AltitudeStream(F) \
    ST_STRAVA_STREAM_HEADER_FIELDS(F)      \
    F(std::vector<float32>, data)

struct AltitudeStream {
    ST_STRAVA_FIELDS_AltitudeStream(ST_STRAVA_FIELD_MEMBER)

        static Decoded<AltitudeStream> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_SmoothVelocityStream(F) \
    ST_STRAVA_STREAM_HEADER_FIELDS(F)            \
    F(std::vector<float32>, data)

struct SmoothVelocityStream {
    ST_STRAVA_FIELDS_SmoothVelocityStream(ST_STRAVA_FIELD_MEMBER)

        static Decoded<SmoothVelocityStream> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_HeartrateStream(F) \
    ST_STRAVA_STREAM_HEADER_FIELDS(F)       \
    F(std::vector<i32>, data)

struct HeartrateStream {
    ST_STRAVA_FIELDS_HeartrateStream(ST_STRAVA_FIELD_MEMBER)

        static Decoded<HeartrateStream> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_CadenceStream(F) \
    ST_STRAVA_STREAM_HEADER_FIELDS(F)     \
    F(std::vector<i32>, data)

struct CadenceStream {
    ST_STRAVA_FIELDS_CadenceStream(ST_STRAVA_FIELD_MEMBER)

        static Decoded<CadenceStream> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_PowerStream(F) \
    ST_STRAVA_STREAM_HEADER_FIELDS(F)   \
    F(std::vector<i32>, data)

struct PowerStream {
    ST_STRAVA_FIELDS_PowerStream(ST_STRAVA_FIELD_MEMBER)

        static Decoded<PowerStream> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_TemperatureStream(F) \
    ST_STRAVA_STREAM_HEADER_FIELDS(F)         \
    F(std::vector<i32>, data)

struct TemperatureStream {
    ST_STRAVA_FIELDS_TemperatureStream(ST_STRAVA_FIELD_MEMBER)

        static Decoded<TemperatureStream> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_MovingStream(F) \
    ST_STRAVA_STREAM_HEADER_FIELDS(F)    \
    F(std::vector<bool>, data)

struct MovingStream {
    ST_STRAVA_FIELDS_MovingStream(ST_STRAVA_FIELD_MEMBER)

        static Decoded<MovingStream> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_SmoothGradeStream(F) \
    ST_STRAVA_STREAM_HEADER_FIELDS(F)         \
    F(std::vector<float32>, data)

struct SmoothGradeStream {
    ST_STRAVA_FIELDS_SmoothGradeStream(ST_STRAVA_FIELD_MEMBER)

        static Decoded<SmoothGradeStream> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_StreamSet(F)        \
    F(TimeStream, time)                      \
    F(DistanceStream, distance)              \
    F(LatLngStream, latlng)                  \
    F(AltitudeStream, altitude)              \
    F(SmoothVelocityStream, velocity_smooth) \
    F(HeartrateStream, heartrate)            \
    F(CadenceStream, cadence)                \
    F(PowerStream, watts)                    \
    F(TemperatureStream, temp)               \
    F(MovingStream, moving)                  \
    F(SmoothGradeStream, grade_smooth)

struct StreamSet {
    ST_STRAVA_FIELDS_StreamSet(ST_STRAVA_FIELD_MEMBER)

        static Decoded<StreamSet> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

}
