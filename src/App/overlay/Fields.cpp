/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <array>
#include <cmath>
#include <format>

#include <overlay/Fields.h>

namespace ST {

namespace {

constexpr double MetersPerMile = 1609.344;

constexpr std::array LaterFields {
    std::string_view { "pace" },
    std::string_view { "altitude" },
    std::string_view { "grade" },
    std::string_view { "temperature" },
    std::string_view { "lr_balance" },
    std::string_view { "time_of_day" },
    std::string_view { "lap" },
    std::string_view { "title" },
    std::string_view { "date" },
};

constexpr FieldUnit KilometersPerHour { .name = "km/h", .factor = 3.6 };
constexpr FieldUnit MilesPerHour { .name = "mph", .factor = 3600.0 / MetersPerMile };
constexpr FieldUnit MetersPerSecond { .name = "m/s", .factor = 1.0 };
constexpr FieldUnit Kilometers { .name = "km", .factor = 0.001 };
constexpr FieldUnit Miles { .name = "mi", .factor = 1.0 / MetersPerMile };
constexpr FieldUnit Meters { .name = "m", .factor = 1.0 };
constexpr FieldUnit Watts { .name = "W" };
constexpr FieldUnit BeatsPerMinute { .name = "bpm" };
constexpr FieldUnit RevolutionsPerMinute { .name = "rpm" };
// FIT records the strides of one foot, which runners count as steps.
constexpr FieldUnit StepsPerMinute { .name = "spm", .factor = 2.0 };
constexpr FieldUnit NoUnit { };

std::string hours_minutes_seconds(double seconds)
{
    auto const total = static_cast<long>(std::max(0.0, std::round(seconds)));
    return std::format("{}:{:02}:{:02}", total / 3600, (total / 60) % 60, total % 60);
}

}

std::string_view OverlayField_name(OverlayField field)
{
    switch (field) {
#undef S
#define S(F)              \
    case OverlayField::F: \
        return #F;
        OVERLAYFIELDS(S)
#undef S
    }
    return "";
}

std::optional<OverlayField> overlay_field(std::string_view name)
{
#undef S
#define S(F)        \
    if (name == #F) \
        return OverlayField::F;
    OVERLAYFIELDS(S)
#undef S
    return { };
}

bool is_later_field(std::string_view name)
{
    return std::ranges::contains(LaterFields, name);
}

FieldUnit default_unit(OverlayField field, Units units, sport sport)
{
    auto const metric = units == Units::Metric;
    switch (field) {
    case OverlayField::speed:
        return metric ? KilometersPerHour : MilesPerHour;
    case OverlayField::distance:
        return metric ? Kilometers : Miles;
    case OverlayField::power:
        return Watts;
    case OverlayField::heart_rate:
        return BeatsPerMinute;
    case OverlayField::cadence:
        return (sport == sport::Running) ? StepsPerMinute : RevolutionsPerMinute;
    case OverlayField::elapsed:
        return NoUnit;
    }
    return NoUnit;
}

std::optional<FieldUnit> field_unit(OverlayField field, std::string_view name)
{
    auto const find = [name](std::initializer_list<FieldUnit> units) -> std::optional<FieldUnit> {
        for (auto const &unit : units) {
            if (unit.name == name) {
                return unit;
            }
        }
        return { };
    };
    switch (field) {
    case OverlayField::speed:
        return find({ KilometersPerHour, MilesPerHour, MetersPerSecond });
    case OverlayField::distance:
        return find({ Kilometers, Miles, Meters });
    case OverlayField::power:
        return find({ Watts });
    case OverlayField::heart_rate:
        return find({ BeatsPerMinute });
    case OverlayField::cadence:
        return find({ RevolutionsPerMinute, StepsPerMinute });
    case OverlayField::elapsed:
        return find({ NoUnit });
    }
    return { };
}

std::string_view default_spec(OverlayField field)
{
    switch (field) {
    case OverlayField::speed:
    case OverlayField::distance:
        return ".1f";
    default:
        return ".0f";
    }
}

// std::vformat says a spec is wrong only by throwing.
bool is_valid_spec(std::string_view spec)
{
    try {
        auto const value = 1.0;
        (void) std::vformat(std::format("{{:{}}}", spec), std::make_format_args(value));
        return true;
    } catch (std::format_error const &) {
        return false;
    }
}

std::optional<double> field_value(Activity const &activity, size_t record, OverlayField field)
{
    if (record >= activity.records.size()) {
        return { };
    }
    auto const &r = activity.records[record];
    auto const  as_double = [](auto const &value) -> std::optional<double> {
        if (!value) {
            return { };
        }
        return static_cast<double>(*value);
    };
    switch (field) {
    case OverlayField::speed:
        return as_double(r.speed);
    case OverlayField::power:
        return as_double(r.power);
    case OverlayField::heart_rate:
        return as_double(r.heart_rate);
    case OverlayField::cadence:
        return as_double(r.cadence);
    case OverlayField::distance:
        return as_double(r.distance);
    case OverlayField::elapsed:
        return static_cast<double>(r.timestamp.timestamp) - static_cast<double>(activity.segment.start_time.timestamp);
    }
    return { };
}

std::string format_field(OverlayField field, std::optional<double> value, FieldUnit const &unit, std::string_view spec, std::string_view missing)
{
    if (!value) {
        return std::string { missing };
    }
    if (field == OverlayField::elapsed) {
        return hours_minutes_seconds(*value);
    }
    auto const shown = *value * unit.factor;
    return std::vformat(std::format("{{:{}}}", spec), std::make_format_args(shown));
}

}
