/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/enums.zig.
 *
 * Assumption: `SportType::to_fit()` (zig: `SportType.toFit`) returns
 * `ST::sport`, the not-yet-translated zig/fit_profile.zig `pub const sport =
 * enum(u8) { ... }`. The enumerator names used in the switch below
 * (`alpine_skiing`, `cross_country_skiing`, `racket`, `paddling`, `training`,
 * `e_biking`, `fitness_equipment`, `golf`, `cycling`, `hiit`, `hiking`,
 * `ice_skating`, `inline_skating`, `kayaking`, `kitesurfing`,
 * `rock_climbing`, `rowing`, `running`, `sailing`, `generic`, `snowboarding`,
 * `snowshoeing`, `soccer`, `stand_up_paddleboarding`, `surfing`, `swimming`,
 * `tennis`, `volleyball`, `walking`, `wheelchair_push_run`, `windsurfing`)
 * are taken verbatim from that zig file's `sport` enum and are not yet
 * defined anywhere in this C++ tree.
 */

#pragma once

#include <array>
#include <optional>
#include <string_view>
#include <utility>

#include <JSON.h>

#include "StravaJson.h"

namespace ST {

// zig/fit_profile.zig: pub const sport = enum(u8) { ... }; not yet
// translated. Forward-declared only so SportType::to_fit()'s declaration can
// name its return type; its definition (in Enums.cpp) is therefore left
// uncompiled until FITProfile.h exists.
enum class sport : u8;

namespace strava {

enum class ActivityType {
    AlpineSki,
    BackcountrySki,
    Canoeing,
    Crossfit,
    EBikeRide,
    Elliptical,
    Golf,
    Handcycle,
    Hike,
    IceSkate,
    InlineSkate,
    Kayaking,
    Kitesurf,
    NordicSki,
    Ride,
    RockClimbing,
    RollerSki,
    Rowing,
    Run,
    Sail,
    Skateboard,
    Snowboard,
    Snowshoe,
    Soccer,
    StairStepper,
    StandUpPaddling,
    Surfing,
    Swim,
    Velomobile,
    VirtualRide,
    VirtualRun,
    Walk,
    WeightTraining,
    Wheelchair,
    Windsurf,
    Workout,
    Yoga,
};

enum class SportType {
    AlpineSki,
    BackcountrySki,
    Badminton,
    Canoeing,
    Crossfit,
    EBikeRide,
    EMountainBikeRide,
    Elliptical,
    Golf,
    GravelRide,
    Handcycle,
    HighIntensityIntervalTraining,
    Hike,
    IceSkate,
    InlineSkate,
    Kayaking,
    Kitesurf,
    MountainBikeRide,
    NordicSki,
    Padel,
    PhysicalTherapy,
    Pickleball,
    Pilates,
    Racquetball,
    Ride,
    RockClimbing,
    RollerSki,
    Rowing,
    Run,
    Sail,
    Skateboard,
    Snowboard,
    Snowshoe,
    Soccer,
    Squash,
    StairStepper,
    StandUpPaddling,
    Surfing,
    Swim,
    TableTennis,
    Tennis,
    TrailRun,
    Velomobile,
    VirtualRide,
    VirtualRow,
    VirtualRun,
    Volleyball,
    Walk,
    WeightTraining,
    Wheelchair,
    Windsurf,
    Workout,
    Yoga,
};

// zig: SportType.toFit(). See the header comment for the ST::sport
// dependency this has not yet got a definition for.
ST::sport to_fit(SportType sport);

enum class Sex {
    M,
    F,
};

enum class MeasurementPreference {
    feet,
    meters,
};

enum class ClubSportType {
    cycling,
    running,
    triathlon,
    other,
};

enum class SegmentActivityType {
    Ride,
    Run,
    VirtualRide,
    VirtualRun,
};

enum class StreamResolution {
    low,
    medium,
    high,
};

enum class StreamSeriesType {
    distance,
    time,
};

enum class ActivityZoneType {
    heartrate,
    power,
};

// JSON values are "NC", "4", "3", "2", "1", "HC" (zig: `@"4"` etc., which are
// not valid C++ identifiers, hence the spelled-out names here).
enum class ClimbCategoryDesc {
    NC,
    Four,
    Three,
    Two,
    One,
    HC,
};

enum class Membership {
    member,
    pending,
};

}

template<>
Decoded<strava::ActivityType> decode<strava::ActivityType>(JSONValue const &json);
template<>
JSONValue encode<strava::ActivityType>(strava::ActivityType const &value);

template<>
Decoded<strava::SportType> decode<strava::SportType>(JSONValue const &json);
template<>
JSONValue encode<strava::SportType>(strava::SportType const &value);

template<>
Decoded<strava::Sex> decode<strava::Sex>(JSONValue const &json);
template<>
JSONValue encode<strava::Sex>(strava::Sex const &value);

template<>
Decoded<strava::MeasurementPreference> decode<strava::MeasurementPreference>(JSONValue const &json);
template<>
JSONValue encode<strava::MeasurementPreference>(strava::MeasurementPreference const &value);

template<>
Decoded<strava::ClubSportType> decode<strava::ClubSportType>(JSONValue const &json);
template<>
JSONValue encode<strava::ClubSportType>(strava::ClubSportType const &value);

template<>
Decoded<strava::SegmentActivityType> decode<strava::SegmentActivityType>(JSONValue const &json);
template<>
JSONValue encode<strava::SegmentActivityType>(strava::SegmentActivityType const &value);

template<>
Decoded<strava::StreamResolution> decode<strava::StreamResolution>(JSONValue const &json);
template<>
JSONValue encode<strava::StreamResolution>(strava::StreamResolution const &value);

template<>
Decoded<strava::StreamSeriesType> decode<strava::StreamSeriesType>(JSONValue const &json);
template<>
JSONValue encode<strava::StreamSeriesType>(strava::StreamSeriesType const &value);

template<>
Decoded<strava::ActivityZoneType> decode<strava::ActivityZoneType>(JSONValue const &json);
template<>
JSONValue encode<strava::ActivityZoneType>(strava::ActivityZoneType const &value);

template<>
Decoded<strava::ClimbCategoryDesc> decode<strava::ClimbCategoryDesc>(JSONValue const &json);
template<>
JSONValue encode<strava::ClimbCategoryDesc>(strava::ClimbCategoryDesc const &value);

template<>
Decoded<strava::Membership> decode<strava::Membership>(JSONValue const &json);
template<>
JSONValue encode<strava::Membership>(strava::Membership const &value);

}
