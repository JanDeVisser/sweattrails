/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/activity.zig.
 *
 * zig's `SummaryActivity.private`/`DetailedActivity.private` fields can't be
 * named `private` in C++; see Club.h for the same situation.
 *
 * `using Activity = DetailedActivity;` matches
 * `pub const Activity = @import("strava/activity.zig").DetailedActivity;`
 * from zig/connect/strava.zig, which is what ../Strava.h and
 * ../../storage/Activity.h assumed as `ST::strava::Activity` before this
 * file existed.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <JSON.h>

#include "Common.h"
#include "Enums.h"
#include "Gear.h"
#include "Segment.h"
#include "StravaJson.h"
#include "Streams.h"

namespace ST::strava {

#define ST_STRAVA_FIELDS_Lap(F)      \
    F(i64, id)                       \
    F(MetaActivity, activity)        \
    F(MetaAthlete, athlete)          \
    F(float32, average_cadence)      \
    F(float32, average_speed)        \
    F(float32, distance)             \
    F(i32, elapsed_time)             \
    F(i32, start_index)              \
    F(i32, end_index)                \
    F(i32, lap_index)                \
    F(float32, max_speed)            \
    F(i32, moving_time)              \
    F(std::string, name)             \
    F(i32, pace_zone)                \
    F(i32, split)                    \
    F(std::string, start_date)       \
    F(std::string, start_date_local) \
    F(float32, total_elevation_gain)

struct Lap {
    ST_STRAVA_FIELDS_Lap(ST_STRAVA_FIELD_MEMBER)

        static Decoded<Lap> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_Split(F)    \
    F(float32, average_speed)        \
    F(float32, distance)             \
    F(i32, elapsed_time)             \
    F(float32, elevation_difference) \
    F(i32, pace_zone)                \
    F(i32, moving_time)              \
    F(i32, split)

struct Split {
    ST_STRAVA_FIELDS_Split(ST_STRAVA_FIELD_MEMBER)

        static Decoded<Split> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_SummaryActivity(F) \
    F(i64, id)                              \
    F(std::string, external_id)             \
    F(i64, upload_id)                       \
    F(std::string, upload_id_str)           \
    F(MetaAthlete, athlete)                 \
    F(std::string, name)                    \
    F(float32, distance)                    \
    F(i32, moving_time)                     \
    F(i32, elapsed_time)                    \
    F(float32, total_elevation_gain)        \
    F(float32, elev_high)                   \
    F(float32, elev_low)                    \
    F(ActivityType, type)                   \
    F(SportType, sport_type)                \
    F(std::string, start_date)              \
    F(std::string, start_date_local)        \
    F(std::string, timezone)                \
    F(LatLng, start_latlng)                 \
    F(LatLng, end_latlng)                   \
    F(i32, achievement_count)               \
    F(i32, kudos_count)                     \
    F(i32, comment_count)                   \
    F(i32, athlete_count)                   \
    F(i32, photo_count)                     \
    F(i32, total_photo_count)               \
    F(PolylineMap, map)                     \
    F(bool, trainer)                        \
    F(bool, commute)                        \
    F(bool, manual)                         \
    F(bool, flagged)                        \
    F(i32, workout_type)                    \
    F(float32, average_speed)               \
    F(float32, max_speed)                   \
    F(bool, has_kudoed)                     \
    F(bool, hide_from_home)                 \
    F(std::string, gear_id)                 \
    F(float32, kilojoules)                  \
    F(float32, average_watts)               \
    F(bool, device_watts)                   \
    F(i32, max_watts)                       \
    F(i32, weighted_average_watts)

struct SummaryActivity {
    ST_STRAVA_FIELDS_SummaryActivity(ST_STRAVA_FIELD_MEMBER)
        std::optional<bool> is_private { }; // zig: `private`

    static Decoded<SummaryActivity> decode(JSONValue const &json);
    [[nodiscard]] JSONValue         encode() const;
};

#define ST_STRAVA_FIELDS_DetailedActivity(F)               \
    F(i64, id)                                             \
    F(std::string, external_id)                            \
    F(i64, upload_id)                                      \
    F(std::string, upload_id_str)                          \
    F(MetaAthlete, athlete)                                \
    F(std::string, name)                                   \
    F(float32, distance)                                   \
    F(i32, moving_time)                                    \
    F(i32, elapsed_time)                                   \
    F(float32, total_elevation_gain)                       \
    F(float32, elev_high)                                  \
    F(float32, elev_low)                                   \
    F(ActivityType, type)                                  \
    F(SportType, sport_type)                               \
    F(std::string, start_date)                             \
    F(std::string, start_date_local)                       \
    F(std::string, timezone)                               \
    F(LatLng, start_latlng)                                \
    F(LatLng, end_latlng)                                  \
    F(i32, achievement_count)                              \
    F(i32, kudos_count)                                    \
    F(i32, comment_count)                                  \
    F(i32, athlete_count)                                  \
    F(i32, photo_count)                                    \
    F(i32, total_photo_count)                              \
    F(PolylineMap, map)                                    \
    F(bool, trainer)                                       \
    F(bool, commute)                                       \
    F(bool, manual)                                        \
    F(bool, flagged)                                       \
    F(i32, workout_type)                                   \
    F(float32, average_speed)                              \
    F(float32, max_speed)                                  \
    F(bool, has_kudoed)                                    \
    F(bool, hide_from_home)                                \
    F(std::string, gear_id)                                \
    F(float32, kilojoules)                                 \
    F(float32, average_watts)                              \
    F(bool, device_watts)                                  \
    F(i32, max_watts)                                      \
    F(i32, weighted_average_watts)                         \
    F(std::string, description)                            \
    F(PhotosSummary, photos)                               \
    F(SummaryGear, gear)                                   \
    F(float32, calories)                                   \
    F(std::vector<DetailedSegmentEffort>, segment_efforts) \
    F(std::vector<DetailedSegmentEffort>, best_efforts)    \
    F(std::string, device_name)                            \
    F(std::string, embed_token)                            \
    F(std::vector<Split>, splits_metric)                   \
    F(std::vector<Split>, splits_standard)                 \
    F(std::vector<Lap>, laps)                              \
    F(StreamSet, streams)

struct DetailedActivity {
    ST_STRAVA_FIELDS_DetailedActivity(ST_STRAVA_FIELD_MEMBER)
        std::optional<bool> is_private { }; // zig: `private`

    static Decoded<DetailedActivity> decode(JSONValue const &json);
    [[nodiscard]] JSONValue          encode() const;
};

// zig: `pub const Activity = ...DetailedActivity;` (see zig/connect/strava.zig).
using Activity = DetailedActivity;

#define ST_STRAVA_FIELDS_UpdatableActivity(F) \
    F(bool, commute)                          \
    F(bool, trainer)                          \
    F(bool, hide_from_home)                   \
    F(std::string, description)               \
    F(std::string, name)                      \
    F(ActivityType, type)                     \
    F(SportType, sport_type)                  \
    F(std::string, gear_id)

struct UpdatableActivity {
    ST_STRAVA_FIELDS_UpdatableActivity(ST_STRAVA_FIELD_MEMBER)

        static Decoded<UpdatableActivity> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

}
