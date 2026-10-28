/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cmath>
#include <numbers>
#include <ranges>

#include <overlay/Shapes.h>
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

u8 tile_level(MapTransform const &transform, float detail)
{
    auto const level = std::lround(std::log2(transform.scale / detail));
    return static_cast<u8>(std::clamp(level, 0l, static_cast<long>(MaxMapZoom)));
}

std::vector<PlacedTile> map_tiles(MapTransform const &transform, Rectangle box, u8 level)
{
    // A tile's side in world pixels at zoom 0, where the transform is.
    auto const count = static_cast<i64>(1) << level;
    auto const side = world_size(0) / static_cast<double>(count);
    auto const to_world = [&transform](float screen, float screen_center, double center) {
        return center + static_cast<double>(screen - screen_center) / transform.scale;
    };
    // The tile a world pixel is in, and the one a box ending there ends
    // with: not the next one, if it ends at a tile's edge.
    auto const first = [side](double world) { return static_cast<i64>(std::floor(world / side)); };
    auto const last = [side](double world) { return static_cast<i64>(std::ceil(world / side)) - 1; };
    auto const left = std::max<i64>(0, first(to_world(box.x, transform.screen_center.x, transform.center.x)));
    auto const right = std::min(count - 1, last(to_world(box.x + box.width, transform.screen_center.x, transform.center.x)));
    auto const top = std::max<i64>(0, first(to_world(box.y, transform.screen_center.y, transform.center.y)));
    auto const bottom = std::min(count - 1, last(to_world(box.y + box.height, transform.screen_center.y, transform.center.y)));

    auto const edge = [&transform, side](i64 x, i64 y) {
        auto const at = transform.to_screen(WorldPoint { .x = static_cast<double>(x) * side, .y = static_cast<double>(y) * side });
        return Vector2 { .x = std::round(at.x), .y = std::round(at.y) };
    };
    std::vector<PlacedTile> ret;
    for (auto y = top; y <= bottom; ++y) {
        for (auto x = left; x <= right; ++x) {
            auto const from = edge(x, y);
            auto const to = edge(x + 1, y + 1);
            ret.push_back(PlacedTile {
                .tile = Tile { .x = static_cast<u32>(x), .y = static_cast<u32>(y), .zoom = level },
                .screen = Rectangle { .x = from.x, .y = from.y, .width = to.x - from.x, .height = to.y - from.y },
            });
        }
    }
    return ret;
}

Rectangle attribution_box(Rectangle panel, ClipKind clip, Vector2 text, float gap, Rectangle area)
{
    auto x = (clip == ClipKind::Circle) ? panel.x + (panel.width - text.x) / 2 : panel.x + panel.width - text.x;
    x = std::max(area.x, std::min(x, area.x + area.width - text.x));
    auto y = panel.y + panel.height + gap;
    if (y + text.y > area.y + area.height) {
        y = panel.y - gap - text.y;
    }
    return Rectangle { .x = x, .y = y, .width = text.x, .height = text.y };
}

ColorMatrix color_filter(TileFilter const &filter)
{
    // Rec. 601's luma: how bright each channel looks.
    constexpr std::array Luma { 0.299f, 0.587f, 0.114f, 0.0f };
    auto const           times = [&Luma](float f) {
        return std::array { Luma[0] * f, Luma[1] * f, Luma[2] * f, 0.0f };
    };
    ColorMatrix ret;
    switch (filter.kind) {
    case TileFilterKind::None:
        break;
    case TileFilterKind::Grayscale:
        ret.rows = { Luma, Luma, Luma, ret.rows[3] };
        break;
    case TileFilterKind::Dark:
        ret.rows = { times(-1), times(-1), times(-1), ret.rows[3] };
        ret.offset = { 1, 1, 1, 0 };
        break;
    case TileFilterKind::Tint: {
        auto const channel = [](unsigned char c) { return static_cast<float>(c) / 255.0f; };
        ret.rows = { times(channel(filter.tint.r)), times(channel(filter.tint.g)), times(channel(filter.tint.b)), ret.rows[3] };
        break;
    }
    }
    return ret;
}

MapShapes map_shapes(ClipKind kind, Rectangle box, float radius, float padding)
{
    return MapShapes {
        .panel = clip_shape(kind, box, radius),
        .track = clip_shape(kind, content_box(box, padding), radius - padding),
    };
}

}
