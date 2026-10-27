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

#include <storage/Activity.h>

namespace ST {

#define OVERLAYFIELDS(S) \
    S(speed)             \
    S(power)             \
    S(heart_rate)        \
    S(cadence)           \
    S(distance)          \
    S(elapsed)

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

// A field the design has that a later slice of the overlay builds, such as
// `lap`. A layout may name it already: it shows as missing.
bool is_later_field(std::string_view name);

enum class Units {
    Metric,
    Imperial,
};

// A unit a field is shown in: its name, and what the field's base unit
// (m/s, m, s, ...) is multiplied by.
struct FieldUnit {
    std::string_view name { };
    double           factor { 1.0 };
};

FieldUnit default_unit(OverlayField field, Units units, sport sport);
// The unit called `name`, if the field can be shown in it.
std::optional<FieldUnit> field_unit(OverlayField field, std::string_view name);
// The std::format spec a field has if the layout gives none.
std::string_view default_spec(OverlayField field);

// Whether std::format takes `spec` for a double.
bool is_valid_spec(std::string_view spec);

// The field's value at the record, in its base unit. Nothing if the record
// doesn't have it. Cadence is as FIT records it, per minute: revolutions,
// or the strides of one foot, which `spm` doubles to steps.
std::optional<double> field_value(Activity const &activity, size_t record, OverlayField field);

// The fields' values for one frame of the overlay, in their base units,
// and the sport, which picks the cadence's unit.
struct FieldValues {
    std::array<std::optional<double>, OverlayFieldCount> values { };
    sport                                                sport { sport::Cycling };

    [[nodiscard]] std::optional<double> operator[](OverlayField field) const
    {
        return values[static_cast<size_t>(field)];
    }
};

// Records further apart than this were a pause, or a lost signal: there's
// nothing to interpolate between them.
constexpr double MaxGap = 5.0;

// The values at the record, as the preview shows them.
FieldValues values_at_record(Activity const &activity, size_t record);

// The values `seconds` after the activity's start, interpolated linearly
// between the records around it, as a video's frames fall between them.
// A field one of them doesn't have, and records more than MaxGap apart,
// give the earlier record's value. `elapsed` is `seconds`.
FieldValues values_at_time(Activity const &activity, double seconds);

// What's shown for a value: converted to the unit and formatted with
// `spec`, which is_valid_spec() has passed; or `missing`. `elapsed` is
// always h:mm:ss.
std::string format_field(OverlayField field, std::optional<double> value, FieldUnit const &unit, std::string_view spec, std::string_view missing);

}
