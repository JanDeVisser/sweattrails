/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/athlete.zig.
 */

#pragma once

#include <string>
#include <vector>

#include <JSON.h>

#include "Club.h"
#include "Enums.h"
#include "Gear.h"
#include "StravaJson.h"

namespace ST::strava {

#define ST_STRAVA_FIELDS_SummaryAthlete(F) \
    F(i64, id)                             \
    F(i32, resource_state)                 \
    F(std::string, firstname)              \
    F(std::string, lastname)               \
    F(std::string, profile_medium)         \
    F(std::string, profile)                \
    F(std::string, city)                   \
    F(std::string, state)                  \
    F(std::string, country)                \
    F(Sex, sex)                            \
    F(bool, premium)                       \
    F(bool, summit)                        \
    F(std::string, created_at)             \
    F(std::string, updated_at)

struct SummaryAthlete {
    ST_STRAVA_FIELDS_SummaryAthlete(ST_STRAVA_FIELD_MEMBER)

        static Decoded<SummaryAthlete> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_DetailedAthlete(F)          \
    F(i64, id)                                       \
    F(i32, resource_state)                           \
    F(std::string, firstname)                        \
    F(std::string, lastname)                         \
    F(std::string, profile_medium)                   \
    F(std::string, profile)                          \
    F(std::string, city)                             \
    F(std::string, state)                            \
    F(std::string, country)                          \
    F(Sex, sex)                                      \
    F(bool, premium)                                 \
    F(bool, summit)                                  \
    F(std::string, created_at)                       \
    F(std::string, updated_at)                       \
    F(i32, follower_count)                           \
    F(i32, friend_count)                             \
    F(MeasurementPreference, measurement_preference) \
    F(i32, ftp)                                      \
    F(float32, weight)                               \
    F(std::vector<SummaryClub>, clubs)               \
    F(std::vector<SummaryGear>, bikes)               \
    F(std::vector<SummaryGear>, shoes)

struct DetailedAthlete {
    ST_STRAVA_FIELDS_DetailedAthlete(ST_STRAVA_FIELD_MEMBER)

        static Decoded<DetailedAthlete> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_ClubAthlete(F) \
    F(i32, resource_state)              \
    F(std::string, firstname)           \
    F(std::string, lastname)            \
    F(std::string, member)              \
    F(bool, admin)                      \
    F(bool, owner)

struct ClubAthlete {
    ST_STRAVA_FIELDS_ClubAthlete(ST_STRAVA_FIELD_MEMBER)

        static Decoded<ClubAthlete> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

}
