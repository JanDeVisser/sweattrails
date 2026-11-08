/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <functional>

#include <Chart.h>

#include <widget/GraphData.h>

namespace ST {

namespace {

std::optional<float> value_of(record const &rec, GraphValue value)
{
    auto const as_float = [](auto const &v) -> std::optional<float> {
        return v ? std::optional { static_cast<float>(*v) } : std::nullopt;
    };
    switch (value) {
    case GraphValue::Altitude:
        return as_float(rec.altitude);
    case GraphValue::Speed:
        return as_float(rec.speed);
    case GraphValue::HeartRate:
        return as_float(real_heart_rate(rec));
    case GraphValue::Power:
        return as_float(rec.power);
    case GraphValue::Cadence:
        return as_float(rec.cadence);
    }
    return { };
}

bool smoothed(GraphValue value)
{
    return value == GraphValue::Speed || value == GraphValue::Power;
}

bool from_zero(GraphValue value)
{
    return value == GraphValue::Speed || value == GraphValue::Power || value == GraphValue::Cadence;
}

// The average over the last GraphSmoothing seconds, of the values since
// the last dropout or pause.
struct RollingAverage {
    struct Sample {
        u32   at;
        float value;
    };
    std::deque<Sample> window { };
    float              sum { 0 };

    void clear()
    {
        window.clear();
        sum = 0;
    }

    float add(u32 at, float value)
    {
        // A clock that goes back would keep every sample after it in the
        // window, until it catches up: start again, as after a pause.
        if (!window.empty() && (at > window.back().at + RangeMovingGap || at < window.back().at)) {
            clear();
        }
        window.push_back({ .at = at, .value = value });
        sum += value;
        while (window.front().at + GraphSmoothing <= at) {
            sum -= window.front().value;
            window.pop_front();
        }
        return sum / static_cast<float>(window.size());
    }
};

// A tick at the first record at or past each step from 0, for a position
// that grows with the records.
std::vector<GraphTick> ticks_at(std::span<record const> records, float width, std::vector<double> const &steps,
    std::function<std::optional<double>(record const &)> const &position, std::function<std::string(double)> const &label)
{
    std::vector<GraphTick> ret;
    std::optional<double>  last;
    for (auto const &rec : records) {
        if (auto const p = position(rec); p) {
            last = std::max(last.value_or(*p), *p);
        }
    }
    if (records.empty() || width <= 0 || !last || *last <= 0) {
        return ret;
    }
    auto const ticks = std::max(1, static_cast<int>(width / GraphTickSpacing));
    auto const step = nice_span(0, *last, ticks, steps).step;
    auto const dt = width / static_cast<float>(records.size());
    auto       next = step;
    for (auto ix = 0uz; ix < records.size() && next <= *last; ++ix) {
        auto const p = position(records[ix]);
        if (!p || *p < next) {
            continue;
        }
        // After a pause that recorded nothing for longer than a step, the
        // record is past several: it gets the last of them.
        auto const passed = std::floor(*p / step) * step;
        ret.push_back({ .x = dt * static_cast<float>(ix), .label = label(passed) });
        next = passed + step;
    }
    return ret;
}

}

GraphSeries graph_series(std::span<record const> recorded, GraphValue value)
{
    GraphSeries    ret;
    RollingAverage average;
    ret.reserve(recorded.size());
    for (auto const &rec : recorded) {
        auto const v = value_of(rec, value);
        if (!v) {
            average.clear();
            ret.emplace_back();
            continue;
        }
        ret.emplace_back(smoothed(value) ? average.add(rec.timestamp.timestamp, *v) : *v);
    }
    return ret;
}

GraphSeries graph_columns(GraphSeries const &series, int width)
{
    GraphSeries ret;
    if (series.empty() || width <= 0) {
        return ret;
    }
    auto const n = series.size();
    auto const w = static_cast<size_t>(width);
    ret.reserve(w);
    for (auto column = 0uz; column < w; ++column) {
        auto const first = column * n / w;
        auto const end = std::max(first + 1, (column + 1) * n / w);
        auto       sum = 0.0f;
        auto       count = 0;
        for (auto ix = first; ix < end; ++ix) {
            if (series[ix]) {
                sum += *series[ix];
                ++count;
            }
        }
        ret.push_back(count > 0 ? std::optional { sum / static_cast<float>(count) } : std::nullopt);
    }
    return ret;
}

std::optional<GraphScale> graph_scale(GraphValue value, std::span<GraphSeries const> series)
{
    std::optional<GraphScale> ret;
    for (auto const &s : series) {
        for (auto const &v : s) {
            if (!v) {
                continue;
            }
            if (!ret) {
                ret = GraphScale { .bottom = *v, .top = *v };
            }
            ret->bottom = std::min(ret->bottom, *v);
            ret->top = std::max(ret->top, *v);
        }
    }
    if (ret && from_zero(value)) {
        if (ret->top <= 0) {
            return { };
        }
        ret->bottom = 0;
    }
    return ret;
}

float graph_y(GraphScale const &scale, float value, float height)
{
    if (scale.top <= scale.bottom) {
        return height / 2;
    }
    return height - (value - scale.bottom) * height / (scale.top - scale.bottom);
}

std::vector<GraphTick> time_ticks(std::span<record const> records, float width)
{
    if (records.empty()) {
        return { };
    }
    auto const start = records.front().timestamp.timestamp;
    return ticks_at(
        records, width, { 60, 120, 300, 600, 900, 1800, 3600, 7200 },
        [start](record const &rec) -> std::optional<double> {
            return static_cast<double>(rec.timestamp.timestamp) - static_cast<double>(start);
        },
        [](double seconds) {
            auto const minutes = static_cast<int>(std::lround(seconds / 60));
            return std::format("{}:{:02}", minutes / 60, minutes % 60);
        });
}

std::vector<GraphTick> distance_ticks(std::span<record const> records, float width)
{
    return ticks_at(
        records, width, { 1000, 2000, 5000, 10000, 20000, 50000, 100000 },
        [](record const &rec) -> std::optional<double> {
            return rec.distance ? std::optional { static_cast<double>(*rec.distance) } : std::nullopt;
        },
        [](double metres) {
            return std::format("{} km", std::lround(metres / 1000));
        });
}

GraphLabel legend_range(Activity const &activity, GraphValue value, GraphScale const &scale)
{
    auto const label = [&scale](std::string_view name, std::string_view unit, float factor, Color color) {
        auto const bottom = std::lround(scale.bottom * factor);
        auto const top = std::lround(scale.top * factor);
        if (bottom == top) {
            return GraphLabel { std::format("{} {} {}", name, top, unit), color };
        }
        return GraphLabel { std::format("{} {}–{} {}", name, bottom, top, unit), color };
    };
    switch (value) {
    case GraphValue::Altitude:
        return label("Alt", "m", 1, AltitudeColor);
    case GraphValue::Speed:
        return label("Spd", "km/h", 3.6f, SpeedColor);
    case GraphValue::HeartRate:
        return label("HR", "bpm", 1, HeartRateColor);
    case GraphValue::Power:
        return label("Pwr", "W", 1, PowerColor);
    case GraphValue::Cadence: {
        auto const cadence = cadence_unit(activity);
        return label("Cad", cadence.unit, static_cast<float>(cadence.scale), CadenceColor);
    }
    }
    return { };
}

std::vector<GraphLabel> legend_values(record const &recorded)
{
    std::vector<GraphLabel> ret;
    if (recorded.altitude) {
        ret.push_back({ std::format("Alt {} m", std::lround(*recorded.altitude)), AltitudeColor });
    }
    if (recorded.speed) {
        ret.push_back({ std::format("Spd {:.1f} km/h", *recorded.speed * 3.6), SpeedColor });
    }
    if (auto const hr = real_heart_rate(recorded); hr) {
        ret.push_back({ std::format("HR {} bpm", *hr), HeartRateColor });
    }
    if (recorded.power) {
        ret.push_back({ std::format("Pwr {} W", *recorded.power), PowerColor });
    }
    return ret;
}

}
