/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cmath>
#include <numbers>
#include <ranges>

#include <overlay/TrackMap.h>

namespace ST {

namespace {

// OSM's street level: further in, GPS isn't more precise.
constexpr double MaxFitScale = 1 << MaxMapZoom;

// The share of the box left around the whole track.
constexpr double FitSpare = 0.1;

Vector2 middle(Rectangle const &r)
{
    return Vector2 { .x = r.x + r.width / 2, .y = r.y + r.height / 2 };
}

}

Vector2 MapTransform::to_screen(WorldPoint const &point) const
{
    return Vector2 {
        .x = screen_center.x + static_cast<float>((point.x - center.x) * scale),
        .y = screen_center.y + static_cast<float>((point.y - center.y) * scale),
    };
}

std::optional<MapTransform> fit_transform(std::vector<OverlaySource::TrackPoint> const &track, ClipShape const &clip, float layout_scale)
{
    if (track.empty()) {
        return { };
    }
    auto const [min_x, max_x] = std::ranges::minmax(track | std::views::transform([](auto const &p) { return p.at.x; }));
    auto const [min_y, max_y] = std::ranges::minmax(track | std::views::transform([](auto const &p) { return p.at.y; }));
    auto const  width = (max_x - min_x) * (1 + FitSpare);
    auto const  height = (max_y - min_y) * (1 + FitSpare);
    auto        scale = MaxFitScale * layout_scale;
    auto const &box = clip.box;
    if (box.width == box.height && 2 * clip.radius >= box.width) {
        // A circle: the bounds' corners are half their diagonal from the
        // middle.
        if (auto const diagonal = std::hypot(width, height); diagonal > 0) {
            scale = std::min(scale, box.width / diagonal);
        }
    } else {
        // A rounded corner cuts into the box this far along each side
        // where the bounds' corner would be.
        auto const cut = 2 * clip.radius * (1 - std::numbers::sqrt2_v<float> / 2);
        if (width > 0) {
            scale = std::min(scale, (box.width - cut) / width);
        }
        if (height > 0) {
            scale = std::min(scale, (box.height - cut) / height);
        }
    }
    return MapTransform {
        .center = WorldPoint { .x = (min_x + max_x) / 2, .y = (min_y + max_y) / 2 },
        .scale = scale,
        .screen_center = middle(box),
    };
}

MapTransform follow_transform(WorldPoint position, int zoom, float layout_scale, Rectangle content)
{
    return MapTransform {
        .center = position,
        .scale = std::ldexp(1.0, zoom) * layout_scale,
        .screen_center = middle(content),
    };
}

SplitTrack split_track(OverlaySource const &source, double seconds)
{
    SplitTrack ret;
    auto const at = position_at(source, seconds);
    if (!at) {
        return ret;
    }
    for (auto const &p : source.track) {
        (p.seconds < seconds ? ret.done : ret.remaining).push_back(p.at);
    }
    ret.done.push_back(*at);
    ret.remaining.insert(ret.remaining.begin(), *at);
    return ret;
}

std::vector<Vector2> screen_points(std::vector<WorldPoint> const &points, MapTransform const &transform)
{
    std::vector<Vector2> ret;
    for (auto ix = 0uz; ix < points.size(); ++ix) {
        auto const p = transform.to_screen(points[ix]);
        auto const last = ix == points.size() - 1;
        if (ret.empty() || last || std::hypot(p.x - ret.back().x, p.y - ret.back().y) >= 1) {
            ret.push_back(p);
        }
    }
    return ret;
}

ClipShape clip_shape(ClipKind kind, Rectangle content, float corner)
{
    switch (kind) {
    case ClipKind::Circle: {
        auto const side = std::min(content.width, content.height);
        auto const at = middle(content);
        return ClipShape { .box = Rectangle { .x = at.x - side / 2, .y = at.y - side / 2, .width = side, .height = side }, .radius = side / 2 };
    }
    case ClipKind::Rounded:
        return ClipShape { .box = content, .radius = std::clamp(corner, 0.0f, std::min(content.width, content.height) / 2) };
    case ClipKind::Rect:
        break;
    }
    return ClipShape { .box = content };
}

}
