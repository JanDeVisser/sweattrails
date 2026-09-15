/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/enums.zig.
 */

#include <array>
#include <utility>

#include "Enums.h"

namespace ST {
namespace strava {
namespace {

using ActivityTypeEntry = std::pair<ActivityType, std::string_view>;
constexpr ActivityTypeEntry ActivityTypeTable[] = {
    { ActivityType::AlpineSki, "AlpineSki" },
    { ActivityType::BackcountrySki, "BackcountrySki" },
    { ActivityType::Canoeing, "Canoeing" },
    { ActivityType::Crossfit, "Crossfit" },
    { ActivityType::EBikeRide, "EBikeRide" },
    { ActivityType::Elliptical, "Elliptical" },
    { ActivityType::Golf, "Golf" },
    { ActivityType::Handcycle, "Handcycle" },
    { ActivityType::Hike, "Hike" },
    { ActivityType::IceSkate, "IceSkate" },
    { ActivityType::InlineSkate, "InlineSkate" },
    { ActivityType::Kayaking, "Kayaking" },
    { ActivityType::Kitesurf, "Kitesurf" },
    { ActivityType::NordicSki, "NordicSki" },
    { ActivityType::Ride, "Ride" },
    { ActivityType::RockClimbing, "RockClimbing" },
    { ActivityType::RollerSki, "RollerSki" },
    { ActivityType::Rowing, "Rowing" },
    { ActivityType::Run, "Run" },
    { ActivityType::Sail, "Sail" },
    { ActivityType::Skateboard, "Skateboard" },
    { ActivityType::Snowboard, "Snowboard" },
    { ActivityType::Snowshoe, "Snowshoe" },
    { ActivityType::Soccer, "Soccer" },
    { ActivityType::StairStepper, "StairStepper" },
    { ActivityType::StandUpPaddling, "StandUpPaddling" },
    { ActivityType::Surfing, "Surfing" },
    { ActivityType::Swim, "Swim" },
    { ActivityType::Velomobile, "Velomobile" },
    { ActivityType::VirtualRide, "VirtualRide" },
    { ActivityType::VirtualRun, "VirtualRun" },
    { ActivityType::Walk, "Walk" },
    { ActivityType::WeightTraining, "WeightTraining" },
    { ActivityType::Wheelchair, "Wheelchair" },
    { ActivityType::Windsurf, "Windsurf" },
    { ActivityType::Workout, "Workout" },
    { ActivityType::Yoga, "Yoga" },
};

using SportTypeEntry = std::pair<SportType, std::string_view>;
constexpr SportTypeEntry SportTypeTable[] = {
    { SportType::AlpineSki, "AlpineSki" },
    { SportType::BackcountrySki, "BackcountrySki" },
    { SportType::Badminton, "Badminton" },
    { SportType::Canoeing, "Canoeing" },
    { SportType::Crossfit, "Crossfit" },
    { SportType::EBikeRide, "EBikeRide" },
    { SportType::EMountainBikeRide, "EMountainBikeRide" },
    { SportType::Elliptical, "Elliptical" },
    { SportType::Golf, "Golf" },
    { SportType::GravelRide, "GravelRide" },
    { SportType::Handcycle, "Handcycle" },
    { SportType::HighIntensityIntervalTraining, "HighIntensityIntervalTraining" },
    { SportType::Hike, "Hike" },
    { SportType::IceSkate, "IceSkate" },
    { SportType::InlineSkate, "InlineSkate" },
    { SportType::Kayaking, "Kayaking" },
    { SportType::Kitesurf, "Kitesurf" },
    { SportType::MountainBikeRide, "MountainBikeRide" },
    { SportType::NordicSki, "NordicSki" },
    { SportType::Padel, "Padel" },
    { SportType::PhysicalTherapy, "PhysicalTherapy" },
    { SportType::Pickleball, "Pickleball" },
    { SportType::Pilates, "Pilates" },
    { SportType::Racquetball, "Racquetball" },
    { SportType::Ride, "Ride" },
    { SportType::RockClimbing, "RockClimbing" },
    { SportType::RollerSki, "RollerSki" },
    { SportType::Rowing, "Rowing" },
    { SportType::Run, "Run" },
    { SportType::Sail, "Sail" },
    { SportType::Skateboard, "Skateboard" },
    { SportType::Snowboard, "Snowboard" },
    { SportType::Snowshoe, "Snowshoe" },
    { SportType::Soccer, "Soccer" },
    { SportType::Squash, "Squash" },
    { SportType::StairStepper, "StairStepper" },
    { SportType::StandUpPaddling, "StandUpPaddling" },
    { SportType::Surfing, "Surfing" },
    { SportType::Swim, "Swim" },
    { SportType::TableTennis, "TableTennis" },
    { SportType::Tennis, "Tennis" },
    { SportType::TrailRun, "TrailRun" },
    { SportType::Velomobile, "Velomobile" },
    { SportType::VirtualRide, "VirtualRide" },
    { SportType::VirtualRow, "VirtualRow" },
    { SportType::VirtualRun, "VirtualRun" },
    { SportType::Volleyball, "Volleyball" },
    { SportType::Walk, "Walk" },
    { SportType::WeightTraining, "WeightTraining" },
    { SportType::Wheelchair, "Wheelchair" },
    { SportType::Windsurf, "Windsurf" },
    { SportType::Workout, "Workout" },
    { SportType::Yoga, "Yoga" },
};

using SexEntry = std::pair<Sex, std::string_view>;
constexpr SexEntry SexTable[] = {
    { Sex::M, "M" },
    { Sex::F, "F" },
};

using MeasurementPreferenceEntry = std::pair<MeasurementPreference, std::string_view>;
constexpr MeasurementPreferenceEntry MeasurementPreferenceTable[] = {
    { MeasurementPreference::feet, "feet" },
    { MeasurementPreference::meters, "meters" },
};

using ClubSportTypeEntry = std::pair<ClubSportType, std::string_view>;
constexpr ClubSportTypeEntry ClubSportTypeTable[] = {
    { ClubSportType::cycling, "cycling" },
    { ClubSportType::running, "running" },
    { ClubSportType::triathlon, "triathlon" },
    { ClubSportType::other, "other" },
};

using SegmentActivityTypeEntry = std::pair<SegmentActivityType, std::string_view>;
constexpr SegmentActivityTypeEntry SegmentActivityTypeTable[] = {
    { SegmentActivityType::Ride, "Ride" },
    { SegmentActivityType::Run, "Run" },
    { SegmentActivityType::VirtualRide, "VirtualRide" },
    { SegmentActivityType::VirtualRun, "VirtualRun" },
};

using StreamResolutionEntry = std::pair<StreamResolution, std::string_view>;
constexpr StreamResolutionEntry StreamResolutionTable[] = {
    { StreamResolution::low, "low" },
    { StreamResolution::medium, "medium" },
    { StreamResolution::high, "high" },
};

using StreamSeriesTypeEntry = std::pair<StreamSeriesType, std::string_view>;
constexpr StreamSeriesTypeEntry StreamSeriesTypeTable[] = {
    { StreamSeriesType::distance, "distance" },
    { StreamSeriesType::time, "time" },
};

using ActivityZoneTypeEntry = std::pair<ActivityZoneType, std::string_view>;
constexpr ActivityZoneTypeEntry ActivityZoneTypeTable[] = {
    { ActivityZoneType::heartrate, "heartrate" },
    { ActivityZoneType::power, "power" },
};

using ClimbCategoryDescEntry = std::pair<ClimbCategoryDesc, std::string_view>;
constexpr ClimbCategoryDescEntry ClimbCategoryDescTable[] = {
    { ClimbCategoryDesc::NC, "NC" },
    { ClimbCategoryDesc::Four, "4" },
    { ClimbCategoryDesc::Three, "3" },
    { ClimbCategoryDesc::Two, "2" },
    { ClimbCategoryDesc::One, "1" },
    { ClimbCategoryDesc::HC, "HC" },
};

using MembershipEntry = std::pair<Membership, std::string_view>;
constexpr MembershipEntry MembershipTable[] = {
    { Membership::member, "member" },
    { Membership::pending, "pending" },
};

}

ST::sport to_fit(SportType sport)
{
    switch (sport) {
    case SportType::AlpineSki:
        return ST::sport::alpine_skiing;
    case SportType::BackcountrySki:
        return ST::sport::cross_country_skiing;
    case SportType::Badminton:
        return ST::sport::racket;
    case SportType::Canoeing:
        return ST::sport::paddling;
    case SportType::Crossfit:
        return ST::sport::training;
    case SportType::EBikeRide:
        return ST::sport::e_biking;
    case SportType::EMountainBikeRide:
        return ST::sport::e_biking;
    case SportType::Elliptical:
        return ST::sport::fitness_equipment;
    case SportType::Golf:
        return ST::sport::golf;
    case SportType::GravelRide:
        return ST::sport::cycling;
    case SportType::Handcycle:
        return ST::sport::cycling;
    case SportType::HighIntensityIntervalTraining:
        return ST::sport::hiit;
    case SportType::Hike:
        return ST::sport::hiking;
    case SportType::IceSkate:
        return ST::sport::ice_skating;
    case SportType::InlineSkate:
        return ST::sport::inline_skating;
    case SportType::Kayaking:
        return ST::sport::kayaking;
    case SportType::Kitesurf:
        return ST::sport::kitesurfing;
    case SportType::MountainBikeRide:
        return ST::sport::cycling;
    case SportType::NordicSki:
        return ST::sport::cross_country_skiing;
    case SportType::Padel:
        return ST::sport::racket;
    case SportType::PhysicalTherapy:
        return ST::sport::training;
    case SportType::Pickleball:
        return ST::sport::racket;
    case SportType::Pilates:
        return ST::sport::training;
    case SportType::Racquetball:
        return ST::sport::racket;
    case SportType::Ride:
        return ST::sport::cycling;
    case SportType::RockClimbing:
        return ST::sport::rock_climbing;
    case SportType::RollerSki:
        return ST::sport::inline_skating;
    case SportType::Rowing:
        return ST::sport::rowing;
    case SportType::Run:
        return ST::sport::running;
    case SportType::Sail:
        return ST::sport::sailing;
    case SportType::Skateboard:
        return ST::sport::generic;
    case SportType::Snowboard:
        return ST::sport::snowboarding;
    case SportType::Snowshoe:
        return ST::sport::snowshoeing;
    case SportType::Soccer:
        return ST::sport::soccer;
    case SportType::Squash:
        return ST::sport::racket;
    case SportType::StairStepper:
        return ST::sport::fitness_equipment;
    case SportType::StandUpPaddling:
        return ST::sport::stand_up_paddleboarding;
    case SportType::Surfing:
        return ST::sport::surfing;
    case SportType::Swim:
        return ST::sport::swimming;
    case SportType::TableTennis:
        return ST::sport::racket;
    case SportType::Tennis:
        return ST::sport::tennis;
    case SportType::TrailRun:
        return ST::sport::running;
    case SportType::Velomobile:
        return ST::sport::cycling;
    case SportType::VirtualRide:
        return ST::sport::cycling;
    case SportType::VirtualRow:
        return ST::sport::rowing;
    case SportType::VirtualRun:
        return ST::sport::running;
    case SportType::Volleyball:
        return ST::sport::volleyball;
    case SportType::Walk:
        return ST::sport::walking;
    case SportType::WeightTraining:
        return ST::sport::training;
    case SportType::Wheelchair:
        return ST::sport::wheelchair_push_run;
    case SportType::Windsurf:
        return ST::sport::windsurfing;
    case SportType::Workout:
        return ST::sport::training;
    case SportType::Yoga:
        return ST::sport::generic;
    }
    return ST::sport::generic;
}

}

template<>
Decoded<strava::ActivityType> decode<strava::ActivityType>(JSONValue const &json)
{
    return decode_enum<strava::ActivityType>(json, strava::ActivityTypeTable, "ActivityType");
}
template<>
JSONValue encode<strava::ActivityType>(strava::ActivityType const &value)
{
    return encode_enum(value, std::span<strava::ActivityTypeEntry const> { strava::ActivityTypeTable });
}

template<>
Decoded<strava::SportType> decode<strava::SportType>(JSONValue const &json)
{
    return decode_enum<strava::SportType>(json, strava::SportTypeTable, "SportType");
}
template<>
JSONValue encode<strava::SportType>(strava::SportType const &value)
{
    return encode_enum(value, std::span<strava::SportTypeEntry const> { strava::SportTypeTable });
}

template<>
Decoded<strava::Sex> decode<strava::Sex>(JSONValue const &json)
{
    return decode_enum<strava::Sex>(json, strava::SexTable, "Sex");
}
template<>
JSONValue encode<strava::Sex>(strava::Sex const &value)
{
    return encode_enum(value, std::span<strava::SexEntry const> { strava::SexTable });
}

template<>
Decoded<strava::MeasurementPreference> decode<strava::MeasurementPreference>(JSONValue const &json)
{
    return decode_enum<strava::MeasurementPreference>(json, strava::MeasurementPreferenceTable, "MeasurementPreference");
}
template<>
JSONValue encode<strava::MeasurementPreference>(strava::MeasurementPreference const &value)
{
    return encode_enum(value, std::span<strava::MeasurementPreferenceEntry const> { strava::MeasurementPreferenceTable });
}

template<>
Decoded<strava::ClubSportType> decode<strava::ClubSportType>(JSONValue const &json)
{
    return decode_enum<strava::ClubSportType>(json, strava::ClubSportTypeTable, "ClubSportType");
}
template<>
JSONValue encode<strava::ClubSportType>(strava::ClubSportType const &value)
{
    return encode_enum(value, std::span<strava::ClubSportTypeEntry const> { strava::ClubSportTypeTable });
}

template<>
Decoded<strava::SegmentActivityType> decode<strava::SegmentActivityType>(JSONValue const &json)
{
    return decode_enum<strava::SegmentActivityType>(json, strava::SegmentActivityTypeTable, "SegmentActivityType");
}
template<>
JSONValue encode<strava::SegmentActivityType>(strava::SegmentActivityType const &value)
{
    return encode_enum(value, std::span<strava::SegmentActivityTypeEntry const> { strava::SegmentActivityTypeTable });
}

template<>
Decoded<strava::StreamResolution> decode<strava::StreamResolution>(JSONValue const &json)
{
    return decode_enum<strava::StreamResolution>(json, strava::StreamResolutionTable, "StreamResolution");
}
template<>
JSONValue encode<strava::StreamResolution>(strava::StreamResolution const &value)
{
    return encode_enum(value, std::span<strava::StreamResolutionEntry const> { strava::StreamResolutionTable });
}

template<>
Decoded<strava::StreamSeriesType> decode<strava::StreamSeriesType>(JSONValue const &json)
{
    return decode_enum<strava::StreamSeriesType>(json, strava::StreamSeriesTypeTable, "StreamSeriesType");
}
template<>
JSONValue encode<strava::StreamSeriesType>(strava::StreamSeriesType const &value)
{
    return encode_enum(value, std::span<strava::StreamSeriesTypeEntry const> { strava::StreamSeriesTypeTable });
}

template<>
Decoded<strava::ActivityZoneType> decode<strava::ActivityZoneType>(JSONValue const &json)
{
    return decode_enum<strava::ActivityZoneType>(json, strava::ActivityZoneTypeTable, "ActivityZoneType");
}
template<>
JSONValue encode<strava::ActivityZoneType>(strava::ActivityZoneType const &value)
{
    return encode_enum(value, std::span<strava::ActivityZoneTypeEntry const> { strava::ActivityZoneTypeTable });
}

template<>
Decoded<strava::ClimbCategoryDesc> decode<strava::ClimbCategoryDesc>(JSONValue const &json)
{
    return decode_enum<strava::ClimbCategoryDesc>(json, strava::ClimbCategoryDescTable, "ClimbCategoryDesc");
}
template<>
JSONValue encode<strava::ClimbCategoryDesc>(strava::ClimbCategoryDesc const &value)
{
    return encode_enum(value, std::span<strava::ClimbCategoryDescEntry const> { strava::ClimbCategoryDescTable });
}

template<>
Decoded<strava::Membership> decode<strava::Membership>(JSONValue const &json)
{
    return decode_enum<strava::Membership>(json, strava::MembershipTable, "Membership");
}
template<>
JSONValue encode<strava::Membership>(strava::Membership const &value)
{
    return encode_enum(value, std::span<strava::MembershipEntry const> { strava::MembershipTable });
}
}
