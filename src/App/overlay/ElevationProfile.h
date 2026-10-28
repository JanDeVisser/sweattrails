/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The overlay's elevation profile: which part of the ride it shows, its
 * line, the marker on it, and where its labels go. No drawing, so that
 * tests reach all of it. In pixels, once the renderer has scaled the
 * layout. See specs/overlay-profile.md.
 */

#pragma once

#include <optional>
#include <vector>

#include <raylib.h>

#include <overlay/Fields.h>
#include <overlay/Layout.h>
#include <overlay/Shapes.h>

namespace ST {

// A stretch of the ride, in meters.
struct DistanceRange {
    double from { 0 };
    double to { 0 };
};

// The part of the ride the profile shows: all of it, or the lap the
// moment is in. None without a profile: no records with a distance and
// an altitude.
std::optional<DistanceRange> profile_extent(OverlaySource const &source, ProfileElement const &profile, FieldValues const &values);

// The altitude per pixel column, in the unit shown: the average over the
// column's stretch, widened by half of `smooth` on each side, which is the
// average over the column of the profile smoothed over `smooth` meters.
std::vector<double> profile_columns(OverlaySource const &source, DistanceRange extent, double smooth, FieldUnit const &unit, size_t columns);

// The scale: "auto" is the columns' lowest and highest, which is what's
// drawn. An "auto" scale spans at least `min_span`, from its bottom, or
// down from a `max` that's given.
Bounds profile_bounds(std::vector<double> const &columns, std::optional<double> min, std::optional<double> max, double min_span);

// A point per column, in the middle of it.
std::vector<Vector2> profile_points(std::vector<double> const &columns, Bounds const &bounds, Rectangle content);

// The line before `x` and after it: both have the point at `x`.
struct SplitLine {
    std::vector<Vector2> done { };
    std::vector<Vector2> remaining { };
};

SplitLine split_at(std::vector<Vector2> const &points, float x);

// The marker on the line where `distance` is. None without a distance, or
// with one outside the extent.
std::optional<Vector2> profile_marker(std::vector<Vector2> const &points, DistanceRange extent, std::optional<double> distance, Rectangle content);

// Where the laps after the first start, inside the extent.
std::vector<double> lap_starts(OverlaySource const &source, DistanceRange extent);

// Across the content, where `distance` is.
float profile_x(double distance, DistanceRange extent, Rectangle content);

// The marker's label, `gap` from the marker: above and to the right of it,
// to its left where it would go past the right edge, below it where
// there's no room above, and inside the content either way.
Rectangle label_box(Vector2 marker, float gap, Vector2 size, Rectangle content);

}
