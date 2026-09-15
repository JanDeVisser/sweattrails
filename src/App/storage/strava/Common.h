/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/common.zig.
 */

#pragma once

#include <optional>
#include <string>

#include <JSON.h>

#include "StravaJson.h"

namespace ST::strava {

// zig: `pub const LatLng = [2]f64;`, i.e. `[latitude, longitude]`. Strava may
// return `[]` for unknown positions, in which case decode() yields a
// default-constructed (0, 0) LatLng rather than an error.
struct LatLng {
    double lat { 0 };
    double lon { 0 };

    static Decoded<LatLng>  decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;

    bool operator==(LatLng const &) const = default;
};

#define ST_STRAVA_FIELDS_MetaActivity(F) \
    F(i64, id)

struct MetaActivity {
    ST_STRAVA_FIELDS_MetaActivity(ST_STRAVA_FIELD_MEMBER)

        static Decoded<MetaActivity> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_MetaAthlete(F) \
    F(i64, id)

struct MetaAthlete {
    ST_STRAVA_FIELDS_MetaAthlete(ST_STRAVA_FIELD_MEMBER)

        static Decoded<MetaAthlete> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_MetaClub(F) \
    F(i64, id)                       \
    F(i32, resource_state)           \
    F(std::string, name)

struct MetaClub {
    ST_STRAVA_FIELDS_MetaClub(ST_STRAVA_FIELD_MEMBER)

        static Decoded<MetaClub> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_PolylineMap(F) \
    F(std::string, id)                  \
    F(std::string, polyline)            \
    F(std::string, summary_polyline)

struct PolylineMap {
    ST_STRAVA_FIELDS_PolylineMap(ST_STRAVA_FIELD_MEMBER)

        static Decoded<PolylineMap> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_PhotosSummaryPrimary(F) \
    F(i64, id)                                   \
    F(i32, source)                               \
    F(std::string, unique_id)                    \
    F(JSONValue, urls) /* Map<String,String>, decoded as raw JSON */

struct PhotosSummaryPrimary {
    ST_STRAVA_FIELDS_PhotosSummaryPrimary(ST_STRAVA_FIELD_MEMBER)

        static Decoded<PhotosSummaryPrimary> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_PhotosSummary(F) \
    F(i32, count)                         \
    F(PhotosSummaryPrimary, primary)

struct PhotosSummary {
    ST_STRAVA_FIELDS_PhotosSummary(ST_STRAVA_FIELD_MEMBER)

        static Decoded<PhotosSummary> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

}
