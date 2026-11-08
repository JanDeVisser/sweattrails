/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * What the activity's graph draws, worked out from the records and the
 * graph's width: the lines a pixel column at a time, their scales, the
 * time and distance labels, and the legend. The graph only draws them.
 */

#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include <storage/Activity.h>
#include <widget/DataFields.h>

namespace ST {

enum class GraphValue {
    Altitude,
    Speed,
    HeartRate,
    Power,
    Cadence,
};

// A value per record, or per pixel column. None where there's none: a
// dropout, which breaks the line.
using GraphSeries = std::vector<std::optional<float>>;

// Power and speed are averaged over this many seconds, so that the lines
// aren't one-second noise.
constexpr u32 GraphSmoothing = 5;

// Labels along the graph are about this far apart, in pixels, at least.
constexpr float GraphTickSpacing = 80.0f;

// One value of the records as recorded, smoothed for power and speed. The
// average restarts after a dropout, after a pause of more than
// RangeMovingGap seconds, which recorded nothing, and when the clock goes
// back.
[[nodiscard]] GraphSeries graph_series(std::span<record const> recorded, GraphValue value);

// The series in `width` pixel columns: each the average of its records'
// values, or one record's over several columns if there are fewer
// records than columns. None for a column without values.
[[nodiscard]] GraphSeries graph_columns(GraphSeries const &series, int width);

// The range a line is drawn over: from 0 to the highest value for speed,
// power and cadence, from the lowest to the highest for altitude and
// heart rate.
struct GraphScale {
    float bottom { 0 };
    float top { 0 };

    bool operator==(GraphScale const &) const = default;
};

// The scale of one or more series of a value, those of two recordings
// while comparing. None without values, or without any above 0 where it
// goes from 0.
[[nodiscard]] std::optional<GraphScale> graph_scale(GraphValue value, std::span<GraphSeries const> series);

// Where `value` goes in a plot `height` high, from the top: the middle
// for a flat line.
[[nodiscard]] float graph_y(GraphScale const &scale, float value, float height);

struct GraphTick {
    float       x { 0 }; // From the graph's left edge
    std::string label { };

    bool operator==(GraphTick const &) const = default;
};

// The graph's x is the record, so a tick goes at the first record at or
// past its time or distance, and a pause bunches them. The steps are
// GraphTickSpacing or more apart over the whole width, which a pause can
// undo: the graph leaves out labels that would overlap.

// Elapsed time from the first record, "0:10", "1:30", in hours and
// minutes.
[[nodiscard]] std::vector<GraphTick> time_ticks(std::span<record const> records, float width);
// Distance, "5 km". None without distances.
[[nodiscard]] std::vector<GraphTick> distance_ticks(std::span<record const> records, float width);

// The legend's entry for a line: "Pwr 0–612 W".
[[nodiscard]] GraphLabel legend_range(Activity const &activity, GraphValue value, GraphScale const &scale);
// The legend while hovering starts with where the marked record is: the
// time since the first record, "0:42:10", and its distance, "12.34 km",
// if it has one. None for a mark past the records.
[[nodiscard]] std::vector<GraphLabel> mark_position(std::span<record const> records, size_t mark);
// Then the record's values, "Alt 45 m",
// "Spd 32.4 km/h", "HR 145 bpm", "Pwr 245 W", for those it has.
[[nodiscard]] std::vector<GraphLabel> legend_values(record const &recorded);

}
