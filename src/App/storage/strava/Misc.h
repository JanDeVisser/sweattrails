/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/misc.zig.
 *
 * zig's `Route.private: ?bool` field can't be named `private` in C++; see
 * Club.h for the same situation. zig's `Upload.@"error": ?[]const u8` is
 * quoted only because `error` is a zig keyword; it is not a C++ keyword, so
 * it keeps its name here. `strava::Error` (below) is a plain data struct
 * scoped to `ST::strava`, unrelated to ST::StravaError/std::error etc.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <JSON.h>

#include "Athlete.h"
#include "Common.h"
#include "Segment.h"
#include "StravaJson.h"

namespace ST::strava {

#define ST_STRAVA_FIELDS_Waypoint(F)        \
    F(LatLng, latlng)                       \
    F(LatLng, target_latlng)                \
    F(std::vector<std::string>, categories) \
    F(std::string, title)                   \
    F(std::string, description)             \
    F(float32, distance_into_route)

struct Waypoint {
    ST_STRAVA_FIELDS_Waypoint(ST_STRAVA_FIELD_MEMBER)

        static Decoded<Waypoint> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_Route(F)            \
    F(i64, id)                               \
    F(std::string, id_str)                   \
    F(SummaryAthlete, athlete)               \
    F(std::string, description)              \
    F(float32, distance)                     \
    F(float32, elevation_gain)               \
    F(PolylineMap, map)                      \
    F(std::string, name)                     \
    F(bool, starred)                         \
    F(i32, timestamp)                        \
    F(i32, type)                             \
    F(i32, sub_type)                         \
    F(std::string, created_at)               \
    F(std::string, updated_at)               \
    F(i32, estimated_moving_time)            \
    F(std::vector<SummarySegment>, segments) \
    F(std::vector<Waypoint>, waypoints)

struct Route {
    ST_STRAVA_FIELDS_Route(ST_STRAVA_FIELD_MEMBER)
        std::optional<bool> is_private { }; // zig: `private`

    static Decoded<Route>   decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_Comment(F) \
    F(i64, id)                      \
    F(i64, activity_id)             \
    F(std::string, text)            \
    F(SummaryAthlete, athlete)      \
    F(std::string, created_at)

struct Comment {
    ST_STRAVA_FIELDS_Comment(ST_STRAVA_FIELD_MEMBER)

        static Decoded<Comment> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_Upload(F) \
    F(i64, id)                     \
    F(std::string, id_str)         \
    F(std::string, external_id)    \
    F(std::string, error)          \
    F(std::string, status)         \
    F(i64, activity_id)

struct Upload {
    ST_STRAVA_FIELDS_Upload(ST_STRAVA_FIELD_MEMBER)

        static Decoded<Upload> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_Error(F) \
    F(std::string, code)          \
    F(std::string, field)         \
    F(std::string, resource)

struct Error {
    ST_STRAVA_FIELDS_Error(ST_STRAVA_FIELD_MEMBER)

        static Decoded<Error> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_Fault(F) \
    F(std::vector<Error>, errors) \
    F(std::string, message)

struct Fault {
    ST_STRAVA_FIELDS_Fault(ST_STRAVA_FIELD_MEMBER)

        static Decoded<Fault> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

}
