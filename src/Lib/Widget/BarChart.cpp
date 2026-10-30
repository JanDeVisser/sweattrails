/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cmath>

#include <App.h>
#include <BarChart.h>

namespace ST {

BarChart::BarChart(pWidget const &parent)
    : Widget(parent, SizePolicy::Stretch, 0)
{
}

void BarChart::set_bars(std::vector<Bar> new_bars)
{
    bars = std::move(new_bars);
    if (highlight && *highlight >= bars.size()) {
        highlight.reset();
    }
    // `hovered` stays: new bars under a mouse that didn't move aren't a
    // hover, which would undo a selection made with the keys.
}

Rectangle BarChart::plot_area() const
{
    return Rectangle {
        .x = scale_width,
        .y = ChartMargin,
        .width = std::max(0.0f, viewport.width - scale_width - ChartMargin),
        .height = std::max(0.0f, viewport.height - label_height - 2 * ChartMargin),
    };
}

std::optional<size_t> BarChart::bar_at(float x) const
{
    auto const plot = plot_area();
    if (bars.empty() || plot.width <= 0 || x < plot.x || x >= plot.x + plot.width) {
        return { };
    }
    auto const slot = plot.width / static_cast<float>(bars.size());
    return std::min(static_cast<size_t>((x - plot.x) / slot), bars.size() - 1);
}

ChartScale BarChart::scale() const
{
    auto const max = bars.empty() ? 0.0 : std::ranges::max(bars, { }, &Bar::value).value;
    return nice_scale(max, Ticks, min_step);
}

void BarChart::process_input()
{
    auto const            mouse = GetMousePosition();
    std::optional<size_t> bar { };
    if (contains(mouse)) {
        auto const y = mouse.y - viewport.y;
        auto const plot = plot_area();
        if (y >= plot.y && y < plot.y + plot.height + label_height) {
            bar = bar_at(mouse.x - viewport.x);
        }
    }
    if (bar != hovered) {
        hovered = bar;
        if (on_hover) {
            on_hover(bar);
        }
    }
    if (bar) {
        App::the()->request_cursor(MOUSE_CURSOR_POINTING_HAND);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && on_click) {
            on_click(*bar);
        }
    }
}

void BarChart::measure()
{
    scale_width = chart_scale_width(scale(), format_value);
    label_height = App::the()->char_size(FontSize::Small).y + ChartMargin;
}

void BarChart::draw()
{
    measure();
    auto const s = scale();
    draw_scale(s);
    auto const plot = plot_area();
    if (bars.empty() || plot.width <= 0 || plot.height <= 0) {
        return;
    }
    auto const slot = plot.width / static_cast<float>(bars.size());
    auto const width = std::max(1.0f, slot * BarFraction);
    for (auto ix = 0uz; ix < bars.size(); ++ix) {
        auto const height = static_cast<float>(bars[ix].value / s.top) * plot.height;
        if (height <= 0) {
            continue;
        }
        auto const x = plot.x + slot * static_cast<float>(ix) + (slot - width) / 2;
        draw_rectangle(x, plot.y + plot.height - height, width, height, ix == highlight ? highlight_color : bar_color);
    }
    draw_labels();
}

void BarChart::draw_scale(ChartScale const &s) const
{
    draw_chart_scale(*this, plot_area(), s, format_value);
}

void BarChart::draw_labels() const
{
    auto const         plot = plot_area();
    auto const         slot = plot.width / static_cast<float>(bars.size());
    std::vector<float> centres;
    std::vector<float> widths;
    for (auto ix = 0uz; ix < bars.size(); ++ix) {
        centres.push_back(plot.x + slot * (static_cast<float>(ix) + 0.5f));
        widths.push_back(bars[ix].label.empty() ? 0 : App::measure_text(bars[ix].label.c_str(), FontSize::Small).x);
    }
    auto const y = plot.y + plot.height + ChartMargin / 2;
    for (auto const ix : visible_labels(centres, widths, App::the()->char_size(FontSize::Small).x)) {
        render_text(centres[ix] - widths[ix] / 2, y, bars[ix].label, FontSize::Small, ChartLabelColor);
    }
}

}
