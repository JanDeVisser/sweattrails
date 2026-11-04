/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * What the bar chart and the line chart share: the scale up the side, and
 * the labels along the bottom.
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <raylib.h>

namespace ST {

struct Widget;

constexpr float ChartMargin = 10.0f;
constexpr Color ChartScaleColor = LIGHTGRAY;
constexpr Color ChartGridColor = { 80, 80, 80, 255 };
constexpr Color ChartLabelColor = LIGHTGRAY;

using FormatValue = std::function<std::string(double)>;

// A scale from `bottom` to `top`, in steps of `step`: 1, 2 or 5 times a
// power of ten, from 0 or below, unless the chart picks its own steps.
struct ChartScale {
    double bottom { 0 };
    double top { 1 };
    double step { 1 };

    bool operator==(ChartScale const &) const = default;
};

// A scale that `max` fits in, in about `ticks` steps, none smaller than
// `min_step`: 1 for a count, so that there are no half activities.
[[nodiscard]] ChartScale nice_scale(double max, int ticks = 5, double min_step = 0);
// One that `min` fits in too, if it's below 0, as a balance can be: the
// line at 0 is drawn brighter than the others.
[[nodiscard]] ChartScale nice_range(double min, double max, int ticks = 5, double min_step = 0);
// One just around `min` and `max`, not from 0, in the smallest of `steps`
// that makes `ticks` of them or fewer: a pace's of 15 s or a minute, say.
[[nodiscard]] ChartScale nice_span(double min, double max, int ticks, std::vector<double> const &steps);

// Which labels, centred at `centres` and as wide as `widths`, can be drawn
// without overlapping the one drawn before, `gap` apart, from left to
// right.
[[nodiscard]] std::vector<size_t> visible_labels(std::vector<float> const &centres, std::vector<float> const &widths, float gap);

// The room the scale's values take left of the plot.
[[nodiscard]] float chart_scale_width(ChartScale const &scale, FormatValue const &format_value);

// A line across the plot, in the widget's coordinates, at each step of the
// scale, with its value to the left.
void draw_chart_scale(Widget const &widget, Rectangle const &plot, ChartScale const &scale, FormatValue const &format_value);

}
