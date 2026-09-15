/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/club.zig.
 *
 * zig's `private: ?bool` field can't be named `private` in C++ (reserved
 * word), so it is declared as `is_private` and kept out of the
 * ST_STRAVA_FIELDS_* X-macro list; its decode/encode are hand-written in
 * Club.cpp instead.
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

#define ST_STRAVA_FIELDS_SummaryClub(F)          \
    F(i64, id)                                   \
    F(i32, resource_state)                       \
    F(std::string, name)                         \
    F(std::string, profile_medium)               \
    F(std::string, cover_photo)                  \
    F(std::string, cover_photo_small)            \
    F(ClubSportType, sport_type)                 \
    F(std::vector<ActivityType>, activity_types) \
    F(std::string, city)                         \
    F(std::string, state)                        \
    F(std::string, country)                      \
    F(i32, member_count)                         \
    F(bool, featured)                            \
    F(bool, verified)                            \
    F(std::string, url)

struct SummaryClub {
    ST_STRAVA_FIELDS_SummaryClub(ST_STRAVA_FIELD_MEMBER)
        std::optional<bool> is_private { }; // zig: `private`

    static Decoded<SummaryClub> decode(JSONValue const &json);
    [[nodiscard]] JSONValue     encode() const;
};

#define ST_STRAVA_FIELDS_DetailedClub(F)         \
    F(i64, id)                                   \
    F(i32, resource_state)                       \
    F(std::string, name)                         \
    F(std::string, profile_medium)               \
    F(std::string, cover_photo)                  \
    F(std::string, cover_photo_small)            \
    F(ClubSportType, sport_type)                 \
    F(std::vector<ActivityType>, activity_types) \
    F(std::string, city)                         \
    F(std::string, state)                        \
    F(std::string, country)                      \
    F(i32, member_count)                         \
    F(bool, featured)                            \
    F(bool, verified)                            \
    F(std::string, url)                          \
    F(Membership, membership)                    \
    F(bool, admin)                               \
    F(bool, owner)                               \
    F(i32, following_count)

struct DetailedClub {
    ST_STRAVA_FIELDS_DetailedClub(ST_STRAVA_FIELD_MEMBER)
        std::optional<bool> is_private { }; // zig: `private`

    static Decoded<DetailedClub> decode(JSONValue const &json);
    [[nodiscard]] JSONValue      encode() const;
};

#define ST_STRAVA_FIELDS_ClubActivity(F) \
    F(MetaAthlete, athlete)              \
    F(std::string, name)                 \
    F(float32, distance)                 \
    F(i32, moving_time)                  \
    F(i32, elapsed_time)                 \
    F(float32, total_elevation_gain)     \
    F(ActivityType, type)                \
    F(SportType, sport_type)             \
    F(i32, workout_type)

struct ClubActivity {
    ST_STRAVA_FIELDS_ClubActivity(ST_STRAVA_FIELD_MEMBER)

        static Decoded<ClubActivity> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

}
