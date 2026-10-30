/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * A bar chart: a bar per value, with a scale up the side and labels along
 * the bottom. A bar can be highlighted, and hovering over or clicking a
 * bar is reported: what that means is up to whoever made the chart.
 */

#pragma once

#include <format>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <raylib.h>

#include <Chart.h>
#include <Widget.h>

namespace ST {

struct Bar {
    std::string label { }; // May be empty
    double      value { 0 };
};

struct BarChart : public Widget {
    static constexpr int   Ticks = 5;
    static constexpr float BarFraction = 0.8f; // Of a bar's slot, the rest a gap

    std::vector<Bar>                           bars { };
    FormatValue                                format_value { [](double v) { return std::format("{:g}", v); } };
    double                                     min_step { 0 };
    std::optional<size_t>                      highlight { };
    std::function<void(std::optional<size_t>)> on_hover { }; // When the bar under the mouse changes
    std::function<void(size_t)>                on_click { };
    Color                                      bar_color { SKYBLUE };
    Color                                      highlight_color { RAYWHITE };

    // Room for the scale on the left and the labels at the bottom, which
    // draw() measures. Tests, which have no fonts, set them themselves.
    float scale_width { 0 };
    float label_height { 0 };

    explicit BarChart(pWidget const &parent);

    void set_bars(std::vector<Bar> new_bars);

    // Where the bars go, in the widget's coordinates.
    [[nodiscard]] Rectangle plot_area() const;
    // The bar at `x`, in the widget's coordinates, if there is one.
    [[nodiscard]] std::optional<size_t> bar_at(float x) const;
    [[nodiscard]] ChartScale            scale() const;

    void process_input() override;
    void draw() override;

private:
    std::optional<size_t> hovered { };

    void measure();
    void draw_scale(ChartScale const &scale) const;
    void draw_labels() const;
};

}
