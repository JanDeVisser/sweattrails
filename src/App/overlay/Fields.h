/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The data an overlay shows: the fields, their units, and how they're
 * formatted. See specs/overlay-layout.md, under Fields.
 */

#pragma once

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
// doesn't have it. Running cadence is in steps, not strides, per minute.
std::optional<double> field_value(Activity const &activity, size_t record, OverlayField field);

// What's shown for a value: converted to the unit and formatted with
// `spec`, which is_valid_spec() has passed; or `missing`. `elapsed` is
// always h:mm:ss.
std::string format_field(OverlayField field, std::optional<double> value, FieldUnit const &unit, std::string_view spec, std::string_view missing);

}
