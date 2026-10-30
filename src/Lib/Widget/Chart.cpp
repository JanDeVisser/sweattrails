/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cmath>
#include <limits>

#include <App.h>
#include <Chart.h>
#include <Widget.h>

namespace ST {

ChartScale nice_scale(double max, int ticks, double min_step)
{
    if (max <= 0 || ticks <= 0) {
        return ChartScale { .top = std::max(1.0, min_step), .step = std::max(1.0, min_step) };
    }
    auto const raw = max / ticks;
    auto const magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    auto const normal = raw / magnitude;
    auto       step = magnitude * (normal <= 1 ? 1 : normal <= 2 ? 2
                              : normal <= 5                      ? 5
                                                                 : 10);
    step = std::max(step, min_step);
    return ChartScale { .top = std::ceil(max / step) * step, .step = step };
}

std::vector<size_t> visible_labels(std::vector<float> const &centres, std::vector<float> const &widths, float gap)
{
    std::vector<size_t> ret;
    auto                end = -std::numeric_limits<float>::infinity();
    for (auto ix = 0uz; ix < std::min(centres.size(), widths.size()); ++ix) {
        if (widths[ix] <= 0) {
            continue;
        }
        auto const left = centres[ix] - widths[ix] / 2;
        if (left < end + gap) {
            continue;
        }
        ret.push_back(ix);
        end = left + widths[ix];
    }
    return ret;
}

float chart_scale_width(ChartScale const &scale, FormatValue const &format_value)
{
    auto widest = 0.0f;
    for (auto value = 0.0; value <= scale.top + scale.step / 2; value += scale.step) {
        widest = std::max(widest, App::measure_text(format_value(value).c_str(), FontSize::Small).x);
    }
    return widest + 2 * ChartMargin;
}

void draw_chart_scale(Widget const &widget, Rectangle const &plot, ChartScale const &scale, FormatValue const &format_value)
{
    auto const text_height = App::the()->char_size(FontSize::Small).y;
    for (auto value = 0.0; value <= scale.top + scale.step / 2; value += scale.step) {
        auto const y = plot.y + plot.height - static_cast<float>(value / scale.top) * plot.height;
        widget.draw_line(plot.x, y, plot.x + plot.width, y, value == 0 ? ChartScaleColor : ChartGridColor);
        auto const text = format_value(value);
        auto const text_width = App::measure_text(text.c_str(), FontSize::Small).x;
        widget.render_text(plot.x - ChartMargin - text_width, y - text_height / 2, text, FontSize::Small, ChartScaleColor);
    }
}

}
