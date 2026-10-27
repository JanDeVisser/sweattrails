/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The data an overlay shows: the fields, their units, and how they're
 * formatted. See specs/overlay-layout.md, under Fields.
 */

#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <storage/Activity.h>

namespace ST {

#define OVERLAYFIELDS(S) \
    S(speed)             \
    S(power)             \
    S(heart_rate)        \
    S(cadence)           \
    S(distance)          \
    S(elapsed)           \
    S(pace)              \
    S(altitude)          \
    S(grade)             \
    S(temperature)       \
    S(lr_balance)        \
    S(time_of_day)       \
    S(lap)               \
    S(title)             \
    S(date)

enum class OverlayField {
#undef S
#define S(F) F,
    OVERLAYFIELDS(S)
#undef S
};

constexpr size_t OverlayFieldCount = 0
#undef S
#define S(F) +1
    OVERLAYFIELDS(S)
#undef S
    ;

std::string_view            OverlayField_name(OverlayField field);
std::optional<OverlayField> overlay_field(std::string_view name);

// How a field is shown, and which specs it takes.
enum class FieldKind {
    Number,   // std::format specs
    Duration, // `hms`, 1:05:09, or `ms`, 65:09
    Clock,    // `hm`, 09:44, or `hms`, 09:44:10
    Date,     // std::chrono specs, such as `%d %b %Y`
    Balance,  // Left/right, 48/52, each side with a std::format spec
    Text,     // As it is, without a spec
};

FieldKind field_kind(OverlayField field);

enum class Units {
    Metric,
    Imperial,
};

// A unit a field is shown in: its name, and how it's had from the field's
// base unit (m/s, m, s, °C, ...): multiplied by `factor`, plus `offset`.
struct FieldUnit {
    std::string_view name { };
    double           factor { 1.0 };
    double           offset { 0.0 };

    [[nodiscard]] double shown(double value) const { return value * factor + offset; }
};

FieldUnit default_unit(OverlayField field, Units units, sport sport);
// The unit called `name`, if the field can be shown in it.
std::optional<FieldUnit> field_unit(OverlayField field, std::string_view name);
// The std::format spec a field has if the layout gives none.
std::string_view default_spec(OverlayField field);

// Whether std::format takes `spec` for a double.
bool is_valid_spec(std::string_view spec);
// Whether the field takes `spec`, for its kind.
bool is_valid_spec(OverlayField field, std::string_view spec);

// What the overlay needs of an activity, worked out once: the preview
// makes one for the activity it shows, a render when it starts. It points
// to the activity, which has to outlive it.
struct OverlaySource {
    // A point of the activity's profile, for the grade.
    struct ProfilePoint {
        double distance { 0 }; // m
        double altitude { 0 }; // m
    };

    explicit OverlaySource(Activity const &activity);

    Activity const           *activity;
    i32                       utc_offset { 0 }; // Local time minus UTC, in seconds
    std::vector<ProfilePoint> profile { };      // The records with both, in order of distance
    // As it was when the source was made: renaming the activity during a
    // render doesn't change the video halfway.
    std::string title { };
    // The fields any record has: one the activity hasn't at all, such as
    // power without a power meter, is missing throughout, also in a pause.
    std::array<bool, OverlayFieldCount> recorded { };

    [[nodiscard]] bool has(OverlayField field) const { return recorded[static_cast<size_t>(field)]; }
};

// The grade the layout shows if it doesn't say: over 30 m, so that a short
// steep ramp keeps most of its grade.
constexpr double DefaultGradeWindow = 30.0;

// The grade at `distance`, in %, over the `window` meters around it: the
// change in altitude over the change in distance. A pause adds no
// distance, so the window reaches past it. Nothing within half the window
// of the ends, or without a profile.
std::optional<double> grade_at(OverlaySource const &source, double distance, double window);

// Local time minus UTC where the activity was: the activity's own, or else
// the system's time zone's at its start.
i32 local_utc_offset(Activity const &activity);

// The field's value at the record, in its base unit. Nothing if the record
// doesn't have it. Cadence is as FIT records it, per minute: revolutions,
// or the strides of one foot, which `spm` doubles to steps. Pace is in s/m,
// `lr_balance` the right leg's share in %, `time_of_day` seconds since
// local midnight, `date` local seconds since the epoch. `title` is no
// number: it's in FieldValues::title.
std::optional<double> field_value(OverlaySource const &source, size_t record, OverlayField field);

// The fields' values for one frame of the overlay, in their base units,
// and the sport, which picks the cadence's unit.
struct FieldValues {
    std::array<std::optional<double>, OverlayFieldCount> values { };
    sport                                                sport { sport::Cycling };
    std::string                                          title { };
    OverlaySource const                                 *source { nullptr }; // For grade()

    [[nodiscard]] std::optional<double> operator[](OverlayField field) const
    {
        return values[static_cast<size_t>(field)];
    }

    // The grade over another window than the default's, at the frame's
    // distance.
    [[nodiscard]] std::optional<double> grade(double window) const
    {
        auto const distance = (*this)[OverlayField::distance];
        return (source && distance) ? grade_at(*source, *distance, window) : std::nullopt;
    }

    // Whether the field has a value: for `title`, that there's a title.
    [[nodiscard]] bool has(OverlayField field) const
    {
        return (field == OverlayField::title) ? !title.empty() : values[static_cast<size_t>(field)].has_value();
    }
};

// Records further apart than this were a pause, or a lost signal: there's
// nothing to interpolate between them.
constexpr double MaxGap = 5.0;

// The values at the record, as the preview shows them.
FieldValues values_at_record(OverlaySource const &source, size_t record);

// The values `seconds` after the activity's start, interpolated linearly
// between the records around it, as a video's frames fall between them.
// A field one of them doesn't have, and records more than MaxGap apart,
// give the earlier record's value. `elapsed` is `seconds`.
FieldValues values_at_time(OverlaySource const &source, double seconds);

// What's shown for a value: converted to the unit and formatted, as the
// field's kind says, with `spec`, which is_valid_spec() has passed; or
// `missing`. Not for `title`, which format_value() shows.
std::string format_field(OverlayField field, std::optional<double> value, FieldUnit const &unit, std::string_view spec, std::string_view missing);

// What's shown for a field of the frame's values, `title` included.
std::string format_value(FieldValues const &values, OverlayField field, FieldUnit const &unit, std::string_view spec, std::string_view missing);

}
