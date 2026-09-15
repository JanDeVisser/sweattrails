/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/gear.zig.
 */

#pragma once

#include <string>

#include <JSON.h>

#include "StravaJson.h"

namespace ST::strava {

#define ST_STRAVA_FIELDS_SummaryGear(F) \
    F(std::string, id)                  \
    F(i32, resource_state)              \
    F(bool, primary)                    \
    F(std::string, name)                \
    F(float32, distance)

struct SummaryGear {
    ST_STRAVA_FIELDS_SummaryGear(ST_STRAVA_FIELD_MEMBER)

        static Decoded<SummaryGear> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

#define ST_STRAVA_FIELDS_DetailedGear(F) \
    F(std::string, id)                   \
    F(i32, resource_state)               \
    F(bool, primary)                     \
    F(std::string, name)                 \
    F(float32, distance)                 \
    F(std::string, brand_name)           \
    F(std::string, model_name)           \
    F(i32, frame_type)                   \
    F(std::string, description)

struct DetailedGear {
    ST_STRAVA_FIELDS_DetailedGear(ST_STRAVA_FIELD_MEMBER)

        static Decoded<DetailedGear> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
};

}
