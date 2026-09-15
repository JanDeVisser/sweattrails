/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/segment.zig.
 *
 * zig's `private: ?bool` fields (SummarySegment, DetailedSegment) can't be
 * named `private` in C++; see Club.h for the same situation.
 *
 * zig doesn't care about declaration order (SummarySegment and
 * DetailedSegmentEffort reference each other's *sibling*, not each other, so
 * there's no real cycle), but `std::optional<T>` requires `T` to be complete
 * wherever it's used as a member, so SummarySegment is declared before
 * DetailedSegmentEffort here, unlike in the zig source.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <JSON.h>

#include "Common.h"
#include "Enums.h"
#include "StravaJson.h"

namespace ST::strava {

#define ST_STRAVA_FIELDS_SummaryPRSegmentEffort(F) \
    F(i64, pr_activity_id)                         \
    F(i32, pr_elapsed_time)                        \
    F(std::string, pr_date)                        \
    F(i32, effort_count)

struct SummaryPRSegmentEffort {
    ST_STRAVA_FIELDS_SummaryPRSegmentEffort(ST_STRAVA_FIELD_MEMBER)

        static Decoded<SummaryPRSegmentEffort> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_SummarySegmentEffort(F) \
    F(i64, id)                                   \
    F(i64, activity_id)                          \
    F(i32, elapsed_time)                         \
    F(std::string, start_date)                   \
    F(std::string, start_date_local)             \
    F(float32, distance)                         \
    F(bool, is_kom)

struct SummarySegmentEffort {
    ST_STRAVA_FIELDS_SummarySegmentEffort(ST_STRAVA_FIELD_MEMBER)

        static Decoded<SummarySegmentEffort> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_SummarySegment(F)       \
    F(i64, id)                                   \
    F(std::string, name)                         \
    F(SegmentActivityType, activity_type)        \
    F(float32, distance)                         \
    F(float32, average_grade)                    \
    F(float32, maximum_grade)                    \
    F(float32, elevation_high)                   \
    F(float32, elevation_low)                    \
    F(LatLng, start_latlng)                      \
    F(LatLng, end_latlng)                        \
    F(i32, climb_category)                       \
    F(std::string, city)                         \
    F(std::string, state)                        \
    F(std::string, country)                      \
    F(SummaryPRSegmentEffort, athlete_pr_effort) \
    F(SummarySegmentEffort, athlete_segment_stats)

struct SummarySegment {
    ST_STRAVA_FIELDS_SummarySegment(ST_STRAVA_FIELD_MEMBER)
        std::optional<bool> is_private { }; // zig: `private`

    static Decoded<SummarySegment> decode(JSONValue const &json);
    [[nodiscard]] JSONValue        encode() const;
};

#define ST_STRAVA_FIELDS_DetailedSegmentEffort(F) \
    F(i64, id)                                    \
    F(i64, activity_id)                           \
    F(i32, elapsed_time)                          \
    F(std::string, start_date)                    \
    F(std::string, start_date_local)              \
    F(float32, distance)                          \
    F(bool, is_kom)                               \
    F(std::string, name)                          \
    F(MetaActivity, activity)                     \
    F(MetaAthlete, athlete)                       \
    F(i32, moving_time)                           \
    F(i32, start_index)                           \
    F(i32, end_index)                             \
    F(float32, average_cadence)                   \
    F(float32, average_watts)                     \
    F(bool, device_watts)                         \
    F(float32, average_heartrate)                 \
    F(float32, max_heartrate)                     \
    F(SummarySegment, segment)                    \
    F(i32, kom_rank)                              \
    F(i32, pr_rank)                               \
    F(bool, hidden)

struct DetailedSegmentEffort {
    ST_STRAVA_FIELDS_DetailedSegmentEffort(ST_STRAVA_FIELD_MEMBER)

        static Decoded<DetailedSegmentEffort> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_DetailedSegment(F)        \
    F(i64, id)                                     \
    F(std::string, name)                           \
    F(SegmentActivityType, activity_type)          \
    F(float32, distance)                           \
    F(float32, average_grade)                      \
    F(float32, maximum_grade)                      \
    F(float32, elevation_high)                     \
    F(float32, elevation_low)                      \
    F(LatLng, start_latlng)                        \
    F(LatLng, end_latlng)                          \
    F(i32, climb_category)                         \
    F(std::string, city)                           \
    F(std::string, state)                          \
    F(std::string, country)                        \
    F(SummaryPRSegmentEffort, athlete_pr_effort)   \
    F(SummarySegmentEffort, athlete_segment_stats) \
    F(std::string, created_at)                     \
    F(std::string, updated_at)                     \
    F(float32, total_elevation_gain)               \
    F(PolylineMap, map)                            \
    F(i32, effort_count)                           \
    F(i32, athlete_count)                          \
    F(bool, hazardous)                             \
    F(i32, star_count)

struct DetailedSegment {
    ST_STRAVA_FIELDS_DetailedSegment(ST_STRAVA_FIELD_MEMBER)
        std::optional<bool> is_private { }; // zig: `private`

    static Decoded<DetailedSegment> decode(JSONValue const &json);
    [[nodiscard]] JSONValue         encode() const;
};

#define ST_STRAVA_FIELDS_ExplorerSegment(F)   \
    F(i64, id)                                \
    F(std::string, name)                      \
    F(i32, climb_category)                    \
    F(ClimbCategoryDesc, climb_category_desc) \
    F(float32, avg_grade)                     \
    F(LatLng, start_latlng)                   \
    F(LatLng, end_latlng)                     \
    F(float32, elev_difference)               \
    F(float32, distance)                      \
    F(std::string, points)

struct ExplorerSegment {
    ST_STRAVA_FIELDS_ExplorerSegment(ST_STRAVA_FIELD_MEMBER)

        static Decoded<ExplorerSegment> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_ExplorerResponse(F) \
    F(std::vector<ExplorerSegment>, segments)

struct ExplorerResponse {
    ST_STRAVA_FIELDS_ExplorerResponse(ST_STRAVA_FIELD_MEMBER)

        static Decoded<ExplorerResponse> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

}
