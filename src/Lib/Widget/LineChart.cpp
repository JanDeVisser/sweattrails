/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>

#include <App.h>
#include <LineChart.h>

namespace ST {

LineChart::LineChart(pWidget const &parent)
    : Widget(parent, SizePolicy::Stretch, 0)
{
}

Rectangle LineChart::plot_area() const
{
    return Rectangle {
        .x = scale_width,
        .y = ChartMargin,
        .width = std::max(0.0f, viewport.width - scale_width - ChartMargin),
        .height = std::max(0.0f, viewport.height - label_height - 2 * ChartMargin),
    };
}

ChartScale LineChart::scale() const
{
    auto max = 0.0;
    for (auto const &line : series) {
        if (!line.values.empty()) {
            max = std::max(max, std::ranges::max(line.values));
        }
    }
    return nice_scale(max, Ticks, min_step);
}

// The first value at the plot's left edge, the last of `x_count` at its
// right.
float LineChart::x_at(double x) const
{
    auto const plot = plot_area();
    if (x_count < 2) {
        return plot.x;
    }
    return plot.x + static_cast<float>(x / static_cast<double>(x_count - 1)) * plot.width;
}

float LineChart::y_at(double value, ChartScale const &s) const
{
    auto const plot = plot_area();
    return plot.y + plot.height - static_cast<float>(value / s.top) * plot.height;
}

void LineChart::draw()
{
    auto const s = scale();
    scale_width = chart_scale_width(s, format_value);
    label_height = App::the()->char_size(FontSize::Small).y + ChartMargin;
    draw_chart_scale(*this, plot_area(), s, format_value);
    draw_ticks();
    // The bold line last, on top of the others.
    auto ordered = series;
    std::ranges::stable_partition(ordered, [](ChartSeries const &line) { return !line.bold; });
    for (auto const &line : ordered) {
        auto const width = line.bold ? BoldWidth : LineWidth;
        for (auto ix = 1uz; ix < line.values.size(); ++ix) {
            auto const from = Vector2 { viewport.x + x_at(static_cast<double>(ix - 1)), viewport.y + y_at(line.values[ix - 1], s) };
            auto const to = Vector2 { viewport.x + x_at(static_cast<double>(ix)), viewport.y + y_at(line.values[ix], s) };
            DrawLineEx(from, to, width, line.color);
        }
        // A line of one value, such as this year's on New Year's Day, has
        // no segment to draw: a dot shows where it is.
        if (line.values.size() == 1) {
            DrawCircleV(Vector2 { viewport.x + x_at(0), viewport.y + y_at(line.values[0], s) }, width, line.color);
        }
    }
    draw_legend();
}

void LineChart::draw_ticks() const
{
    std::vector<float> centres;
    std::vector<float> widths;
    for (auto const &tick : ticks) {
        centres.push_back(x_at(tick.x));
        widths.push_back(App::measure_text(tick.label.c_str(), FontSize::Small).x);
    }
    auto const plot = plot_area();
    auto const y = plot.y + plot.height + ChartMargin / 2;
    for (auto const ix : visible_labels(centres, widths, App::the()->char_size(FontSize::Small).x)) {
        render_text(centres[ix] - widths[ix] / 2, y, ticks[ix].label, FontSize::Small, ChartLabelColor);
    }
}

// The lines' labels in their colours, top left in the plot.
void LineChart::draw_legend() const
{
    auto const plot = plot_area();
    auto const line_height = App::the()->char_size(FontSize::Small).y;
    auto       y = plot.y + ChartMargin;
    for (auto const &line : series) {
        auto const x = plot.x + ChartMargin;
        draw_rectangle(x, y + line_height / 2 - 1, 2 * ChartMargin, line.bold ? BoldWidth : LineWidth, line.color);
        render_text(x + 3 * ChartMargin, y, line.label, FontSize::Small, line.color);
        y += line_height * 1.2f;
    }
}

}
