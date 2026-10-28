/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>

#include <overlay/ElevationProfile.h>

namespace ST {

namespace {

// The first and the last distance of the records from `first` up to `end`.
std::optional<DistanceRange> distances(Activity const &activity, size_t first, size_t end)
{
    std::optional<DistanceRange> ret;
    for (auto ix = first; ix < std::min(end, activity.records.size()); ++ix) {
        if (auto const d = activity.records[ix].distance; d) {
            ret = ret ? DistanceRange { .from = ret->from, .to = *d } : DistanceRange { .from = *d, .to = *d };
        }
    }
    return ret;
}

}

std::optional<DistanceRange> profile_extent(OverlaySource const &source, ProfileElement const &profile, FieldValues const &values)
{
    auto const &points = source.profile;
    if (points.size() < 2 || points.back().distance <= points.front().distance) {
        return { };
    }
    DistanceRange ret { .from = points.front().distance, .to = points.back().distance };
    auto const   &activity = *source.activity;
    auto const    lap = values[OverlayField::lap];
    if (profile.extent == ProfileExtent::All || activity.laps.empty() || !lap) {
        return ret;
    }
    auto const ix = std::clamp(static_cast<size_t>(*lap), 1uz, activity.laps.size()) - 1;
    auto const in_lap = distances(activity, activity.laps[ix].first_record, activity.laps[ix].end_record);
    if (!in_lap) {
        return { };
    }
    // A lap without distances, of none, or past the profile's ends, such as
    // one where the altimeter gave out, has no profile.
    ret = DistanceRange { .from = std::max(ret.from, in_lap->from), .to = std::min(ret.to, in_lap->to) };
    if (ret.to <= ret.from) {
        return { };
    }
    return ret;
}

std::vector<double> profile_columns(OverlaySource const &source, DistanceRange extent, double smooth, FieldUnit const &unit, size_t columns)
{
    std::vector<double> ret;
    auto const          width = (extent.to - extent.from) / static_cast<double>(columns);
    for (auto ix = 0uz; ix < columns; ++ix) {
        auto const left = extent.from + static_cast<double>(ix) * width;
        auto const average = average_altitude(source, left - smooth / 2, left + width + smooth / 2);
        ret.push_back(unit.shown(average.value_or(0)));
    }
    return ret;
}

Bounds profile_bounds(std::vector<double> const &columns, std::optional<double> min, std::optional<double> max, double min_span)
{
    if (min && max) {
        return Bounds { .min = *min, .max = *max };
    }
    auto const [lowest, highest] = columns.empty() ? std::pair { 0.0, 0.0 } : std::pair { *std::ranges::min_element(columns), *std::ranges::max_element(columns) };
    Bounds ret { .min = min.value_or(lowest), .max = max.value_or(highest) };
    if (ret.max - ret.min < min_span) {
        if (!max) {
            ret.max = ret.min + min_span;
        } else {
            ret.min = ret.max - min_span;
        }
    }
    return ret;
}

std::vector<Vector2> profile_points(std::vector<double> const &columns, Bounds const &bounds, Rectangle content)
{
    std::vector<Vector2> ret;
    for (auto ix = 0uz; ix < columns.size(); ++ix) {
        ret.push_back(Vector2 { .x = content.x + static_cast<float>(ix) + 0.5f, .y = graph_y(columns[ix], bounds, content) });
    }
    return ret;
}

namespace {

// The line's height at `x`, between the points around it.
float y_at(std::vector<Vector2> const &points, float x)
{
    auto const after = std::ranges::lower_bound(points, x, { }, &Vector2::x);
    if (after == points.begin()) {
        return after->y;
    }
    if (after == points.end()) {
        return points.back().y;
    }
    auto const &before = *(after - 1);
    return before.y + (x - before.x) / (after->x - before.x) * (after->y - before.y);
}

}

SplitLine split_at(std::vector<Vector2> const &points, float x)
{
    SplitLine ret;
    if (points.empty()) {
        return ret;
    }
    if (x <= points.front().x) {
        ret.remaining = points;
        return ret;
    }
    if (x >= points.back().x) {
        ret.done = points;
        return ret;
    }
    auto const at = Vector2 { .x = x, .y = y_at(points, x) };
    for (auto const &p : points) {
        (p.x < x ? ret.done : ret.remaining).push_back(p);
    }
    ret.done.push_back(at);
    ret.remaining.insert(ret.remaining.begin(), at);
    return ret;
}

float profile_x(double distance, DistanceRange extent, Rectangle content)
{
    auto const fraction = (distance - extent.from) / (extent.to - extent.from);
    return content.x + static_cast<float>(fraction) * content.width;
}

std::optional<Vector2> profile_marker(std::vector<Vector2> const &points, DistanceRange extent, std::optional<double> distance, Rectangle content)
{
    if (points.empty() || !distance || *distance < extent.from || *distance > extent.to) {
        return { };
    }
    auto const x = profile_x(*distance, extent, content);
    return Vector2 { .x = x, .y = y_at(points, x) };
}

std::vector<double> lap_starts(OverlaySource const &source, DistanceRange extent)
{
    std::vector<double> ret;
    auto const         &activity = *source.activity;
    for (auto ix = 1uz; ix < activity.laps.size(); ++ix) {
        auto const lap = distances(activity, activity.laps[ix].first_record, activity.laps[ix].end_record);
        if (lap && lap->from > extent.from && lap->from < extent.to) {
            ret.push_back(lap->from);
        }
    }
    return ret;
}

Rectangle label_box(Vector2 marker, float gap, Vector2 size, Rectangle content)
{
    auto x = marker.x + gap;
    if (x + size.x > content.x + content.width) {
        x = marker.x - gap - size.x;
    }
    auto y = marker.y - gap - size.y;
    if (y < content.y) {
        y = marker.y + gap;
    }
    x = std::max(content.x, std::min(x, content.x + content.width - size.x));
    y = std::max(content.y, std::min(y, content.y + content.height - size.y));
    return Rectangle { .x = x, .y = y, .width = size.x, .height = size.y };
}

}
