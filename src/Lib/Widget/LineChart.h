/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * A line chart: lines of values at x = 0, 1, 2, ..., with a scale up the
 * side, labels along the bottom, and a legend of the lines.
 */

#pragma once

#include <format>
#include <string>
#include <vector>

#include <raylib.h>

#include <Chart.h>
#include <Widget.h>

namespace ST {

struct ChartSeries {
    std::string         label { };
    Color               color { SKYBLUE };
    bool                bold { false };
    std::vector<double> values { }; // At x = 0, 1, 2, ...
};

// A label along the bottom, at an x of the values.
struct ChartTick {
    double      x { 0 };
    std::string label { };
};

struct LineChart : public Widget {
    static constexpr int   Ticks = 5;
    static constexpr float LineWidth = 1.5f;
    static constexpr float BoldWidth = 3.0f;

    std::vector<ChartSeries> series { };
    // How many values wide the x axis is, so that a line with fewer, such
    // as this year's up to today, stops partway.
    size_t                 x_count { 0 };
    std::vector<ChartTick> ticks { };
    FormatValue            format_value { [](double v) { return std::format("{:g}", v); } };
    double                 min_step { 0 };

    // Room for the scale on the left and the labels at the bottom, which
    // draw() measures. Tests, which have no fonts, set them themselves.
    float scale_width { 0 };
    float label_height { 0 };

    explicit LineChart(pWidget const &parent);

    // Where the lines go, in the widget's coordinates.
    [[nodiscard]] Rectangle  plot_area() const;
    [[nodiscard]] ChartScale scale() const;
    // Where a value's x and y are, in the widget's coordinates.
    [[nodiscard]] float x_at(double x) const;
    [[nodiscard]] float y_at(double value, ChartScale const &scale) const;

    void draw() override;

private:
    void draw_ticks() const;
    void draw_legend() const;
};

}
