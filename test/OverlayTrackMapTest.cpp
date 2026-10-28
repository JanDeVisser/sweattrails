/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The overlay's map: fitting and following the track, splitting it at the
 * moment, the points on the screen, and the clip's shape.
 */

#include <cmath>
#include <format>
#include <string>
#include <vector>

#include <overlay/TrackMap.h>
#include <storage/Storage.h>

#include <TestApp.h>

using namespace ST;

namespace {

constexpr u32 Start = 1'750'000'000;

std::string show(Vector2 v)
{
    return std::format("{} {}", v.x, v.y);
}

std::string show(Rectangle r)
{
    return std::format("{} {} {} {}", r.x, r.y, r.width, r.height);
}

std::vector<OverlaySource::TrackPoint> track_of(std::vector<WorldPoint> const &points)
{
    std::vector<OverlaySource::TrackPoint> ret;
    for (auto const &p : points) {
        ret.push_back(OverlaySource::TrackPoint { .seconds = static_cast<double>(ret.size()), .at = p });
    }
    return ret;
}

void test_fit()
{
    Rectangle const content { 0, 0, 110, 110 };
    // Wide: 10 world pixels, with 10% to spare, across 110.
    auto const wide = fit_transform(track_of({ { 0, 0 }, { 10, 1 } }), ClipShape { .box = content }, 1);
    CHECK(wide.has_value());
    if (wide) {
        CHECK_EQ(wide->scale, 10.0);
        CHECK_EQ(show(wide->to_screen({ 5, 0.5 })), "55 55");
        CHECK_EQ(show(wide->to_screen({ 0, 0.5 })), "5 55");
    }
    auto const tall = fit_transform(track_of({ { 0, 0 }, { 1, 10 } }), ClipShape { .box = content }, 1);
    CHECK_EQ(tall ? tall->scale : 0, 10.0);
    // A track of one point: street level, at the layout's scale.
    auto const point = fit_transform(track_of({ { 3, 4 }, { 3, 4 } }), ClipShape { .box = content }, 2);
    CHECK_EQ(point ? point->scale : 0, 524288.0);
    CHECK_EQ(point ? show(point->to_screen({ 3, 4 })) : std::string { }, "55 55");
    CHECK(!fit_transform({ }, ClipShape { .box = content }, 1));
}

// In a content box away from the screen's corner, the track's middle is
// at the box's middle.
void test_fit_in_offset_content()
{
    auto const fit = fit_transform(track_of({ { 0, 0 }, { 10, 1 } }), ClipShape { .box = Rectangle { 100, 200, 110, 220 } }, 1);
    CHECK_EQ(fit ? show(fit->to_screen({ 5, 0.5 })) : std::string { }, "155 310");
}

// The spec's fit has the whole track in the box. In a circle, the box is
// the circle: a ride along the diagonal has its ends in the bounds'
// corners, which are outside a circle as big as the bounds.
void test_fit_in_a_circle()
{
    auto const clip = clip_shape(ClipKind::Circle, Rectangle { 0, 0, 110, 110 }, 0);
    auto const track = track_of({ { 0, 0 }, { 5, 5 }, { 10, 10 } });
    auto const fit = fit_transform(track, clip, 1);
    CHECK(fit.has_value());
    if (!fit) {
        return;
    }
    auto const centre = Vector2 { .x = clip.box.x + clip.radius, .y = clip.box.y + clip.radius };
    for (auto const &p : track) {
        auto const at = fit->to_screen(p.at);
        auto const from_centre = std::hypot(at.x - centre.x, at.y - centre.y);
        CHECK(from_centre <= clip.radius);
    }
}

// A track from corner to corner of its bounds stays clear of a rounded
// clip's corners: inside the circle each corner is rounded with.
void test_fit_in_a_rounded_rectangle()
{
    auto const clip = clip_shape(ClipKind::Rounded, Rectangle { 0, 0, 300, 300 }, 60);
    auto const fit = fit_transform(track_of({ { 0, 0 }, { 10, 10 } }), clip, 1);
    CHECK(fit.has_value());
    if (!fit) {
        return;
    }
    auto const top_left = fit->to_screen({ 0, 0 });
    CHECK(std::hypot(top_left.x - 60, top_left.y - 60) <= 60);
    auto const bottom_right = fit->to_screen({ 10, 10 });
    CHECK(std::hypot(bottom_right.x - 240, bottom_right.y - 240) <= 60);
    // A rectangle has no corners to clear: 10% to spare.
    auto const rect = fit_transform(track_of({ { 0, 0 }, { 10, 10 } }), clip_shape(ClipKind::Rect, Rectangle { 0, 0, 300, 300 }, 60), 1);
    CHECK_EQ(std::format("{:.2f}", rect ? rect->scale : 0), "27.27");
}

void test_follow()
{
    auto const follow = follow_transform({ 100, 200 }, 15, 2, Rectangle { 10, 20, 300, 300 });
    CHECK_EQ(follow.scale, 65536.0);
    CHECK_EQ(show(follow.to_screen({ 100, 200 })), "160 170");
    // A thousandth of a world pixel is 65.5 screen pixels.
    CHECK_EQ(std::format("{:.1f}", follow.to_screen({ 100.001, 200 }).x), "225.5");
}

// Four records a second apart, going east.
struct Ride {
    Storage  storage { };
    Activity activity { storage, ActivityID { } };

    Ride()
    {
        activity.segment.start_time.timestamp = Start;
        for (auto i = 0u; i < 4; ++i) {
            activity.records.push_back(record {
                .timestamp = DateTime { .timestamp = Start + i },
                .position = Coordinates { .lat = 52.0f, .lon = 4.0f + 0.001f * static_cast<float>(i) },
            });
        }
    }
};

void test_split()
{
    Ride                r;
    OverlaySource const source { r.activity };
    auto const         &track = source.track;
    auto const          east = [&track](std::vector<WorldPoint> const &points) {
        // In records from the first, as the position goes east evenly.
        std::string ret;
        auto const  step = track[1].at.x - track[0].at.x;
        for (auto const &p : points) {
            ret += std::format("{}{:.1f}", ret.empty() ? "" : " ", (p.x - track[0].at.x) / step);
        }
        return ret;
    };
    auto const middle = split_track(source, 1.5);
    CHECK_EQ(east(middle.done), "0.0 1.0 1.5");
    CHECK_EQ(east(middle.remaining), "1.5 2.0 3.0");
    // Before the start, all of it remains; after the end, all of it is done.
    CHECK_EQ(east(split_track(source, -1).done), "0.0");
    CHECK_EQ(east(split_track(source, -1).remaining), "0.0 0.0 1.0 2.0 3.0");
    CHECK_EQ(east(split_track(source, 10).done), "0.0 1.0 2.0 3.0 3.0");
    CHECK_EQ(east(split_track(source, 10).remaining), "3.0");
    // At a record, it's the end of done and the start of what remains.
    CHECK_EQ(east(split_track(source, 2).done), "0.0 1.0 2.0");
    CHECK_EQ(east(split_track(source, 2).remaining), "2.0 2.0 3.0");

    Storage             storage;
    Activity            indoors { storage, ActivityID { } };
    OverlaySource const without { indoors };
    CHECK(split_track(without, 1).done.empty());
    CHECK(split_track(without, 1).remaining.empty());
}

void test_screen_points()
{
    MapTransform const transform { .center = { 0, 0 }, .scale = 1, .screen_center = { 0, 0 } };
    auto const         points = screen_points({ { 0, 0 }, { 0.5, 0 }, { 1.2, 0 }, { 1.4, 0 }, { 5, 0 } }, transform);
    CHECK_EQ(points.size(), 3uz);
    if (points.size() == 3) {
        CHECK_EQ(points[1].x, 1.2f);
        CHECK_EQ(points[2].x, 5.0f);
    }
    // The last is there, however close.
    CHECK_EQ(screen_points({ { 0, 0 }, { 0.3, 0 } }, transform).size(), 2uz);
    CHECK(screen_points({ }, transform).empty());
}

void test_clip_shape()
{
    Rectangle const content { 0, 0, 200, 100 };
    auto const      circle = clip_shape(ClipKind::Circle, content, 16);
    CHECK_EQ(show(circle.box), "50 0 100 100");
    CHECK_EQ(circle.radius, 50.0f);
    auto const rounded = clip_shape(ClipKind::Rounded, content, 16);
    CHECK_EQ(show(rounded.box), "0 0 200 100");
    CHECK_EQ(rounded.radius, 16.0f);
    // No more round than a half circle at the ends.
    CHECK_EQ(clip_shape(ClipKind::Rounded, content, 80).radius, 50.0f);
    CHECK_EQ(clip_shape(ClipKind::Rounded, content, -4).radius, 0.0f);
    auto const rect = clip_shape(ClipKind::Rect, content, 16);
    CHECK_EQ(show(rect.box), "0 0 200 100");
    CHECK_EQ(rect.radius, 0.0f);
}

// The track's shape is inside the padding, with its corners rounded as
// much less: a circle stays one, and a corner no rounder than the padding
// is square inside it.
void test_map_shapes()
{
    Rectangle const box { 10, 20, 300, 200 };
    auto const      circle = map_shapes(ClipKind::Circle, box, 16, 10);
    CHECK_EQ(show(circle.panel.box), "60 20 200 200");
    CHECK_EQ(circle.panel.radius, 100.0f);
    CHECK_EQ(show(circle.track.box), "70 30 180 180");
    CHECK_EQ(circle.track.radius, 90.0f);
    auto const rounded = map_shapes(ClipKind::Rounded, box, 16, 10);
    CHECK_EQ(show(rounded.panel.box), "10 20 300 200");
    CHECK_EQ(rounded.panel.radius, 16.0f);
    CHECK_EQ(show(rounded.track.box), "20 30 280 180");
    CHECK_EQ(rounded.track.radius, 6.0f);
    CHECK_EQ(map_shapes(ClipKind::Rounded, box, 8, 10).track.radius, 0.0f);
    auto const rect = map_shapes(ClipKind::Rect, box, 16, 10);
    CHECK_EQ(rect.panel.radius, 0.0f);
    CHECK_EQ(show(rect.track.box), "20 30 280 180");
    CHECK_EQ(rect.track.radius, 0.0f);
}

}

// The level nearest the scale: as asked for at 1080p, one more at 4K, the
// same at 1440p, drawn bigger, and at a render's double size, as at its
// own.
void test_tile_level()
{
    auto const at = [](double scale, float detail) {
        return static_cast<int>(tile_level(MapTransform { .scale = scale }, detail));
    };
    CHECK_EQ(at(std::ldexp(1.0, 15), 1), 15);
    CHECK_EQ(at(std::ldexp(1.0, 16), 1), 16);
    CHECK_EQ(at(std::ldexp(1.0, 15) * 4 / 3, 1), 15);
    CHECK_EQ(at(std::ldexp(1.0, 16), 2), 15);
    CHECK_EQ(at(std::ldexp(1.0, 15) * 1.5, 1), 16);
    CHECK_EQ(at(0.001, 1), 0);
    CHECK_EQ(at(std::ldexp(1.0, 22), 1), 18);
}

std::string show(std::vector<PlacedTile> const &tiles)
{
    std::string ret;
    for (auto const &[tile, screen] : tiles) {
        ret += std::format("{}{}/{}/{} at {}", ret.empty() ? "" : ", ", tile.zoom, tile.x, tile.y, show(screen));
    }
    return ret;
}

void test_map_tiles()
{
    // The world's middle at the box's middle: the four tiles of level 1.
    MapTransform const middle { .center = { 128, 128 }, .scale = 2, .screen_center = { 100, 100 } };
    CHECK_EQ(show(map_tiles(middle, Rectangle { 0, 0, 200, 200 }, 1)),
        "1/0/0 at -156 -156 256 256, 1/1/0 at 100 -156 256 256, 1/0/1 at -156 100 256 256, 1/1/1 at 100 100 256 256");
    // A box ending at a tile's edge doesn't have the next tile.
    CHECK_EQ(map_tiles(middle, Rectangle { 0, 0, 100, 100 }, 1).size(), 1uz);

    // Scaled, a tile is 166.4 px: its edges at whole pixels, shared with
    // the next one.
    MapTransform const scaled { .center = { 128, 128 }, .scale = 1.3, .screen_center = { 100.3f, 100 } };
    auto const         tiles = map_tiles(scaled, Rectangle { 0, 0, 200, 200 }, 1);
    CHECK_EQ(tiles.size(), 4uz);
    if (tiles.size() == 4) {
        CHECK_EQ(show(tiles[0].screen), "-66 -66 166 166");
        CHECK_EQ(show(tiles[1].screen), "100 -66 167 166");
    }

    // Near the world's top left corner: none past its edges.
    MapTransform const corner { .center = { 10, 10 }, .scale = 2, .screen_center = { 100, 100 } };
    CHECK_EQ(show(map_tiles(corner, Rectangle { 0, 0, 200, 200 }, 1)), "1/0/0 at 80 80 256 256");
}

void test_attribution_box()
{
    Rectangle const area { 0, 0, 1920, 1080 };
    Vector2 const   text { 200, 20 };
    Rectangle const panel { 100, 100, 300, 300 };
    CHECK_EQ(show(attribution_box(panel, ClipKind::Rect, text, 4, area)), "200 404 200 20");
    CHECK_EQ(show(attribution_box(panel, ClipKind::Rounded, text, 4, area)), "200 404 200 20");
    CHECK_EQ(show(attribution_box(panel, ClipKind::Circle, text, 4, area)), "150 404 200 20");
    // At the bottom of the frame, above the map.
    CHECK_EQ(show(attribution_box(Rectangle { 100, 760, 300, 300 }, ClipKind::Rect, text, 4, area)), "200 736 200 20");
    // A small circle at the frame's right edge: inside the frame.
    CHECK_EQ(show(attribution_box(Rectangle { 1800, 100, 100, 100 }, ClipKind::Circle, text, 4, area)), "1720 204 200 20");
}

std::string show(Color c)
{
    return std::format("{} {} {} {}", c.r, c.g, c.b, c.a);
}

void test_color_filter()
{
    auto const filter = [](TileFilterKind kind, Color tint = WHITE) {
        return color_filter(TileFilter { .kind = kind, .tint = tint });
    };
    Color const red { 255, 0, 0, 255 };
    CHECK_EQ(show(filter(TileFilterKind::None).apply(red)), "255 0 0 255");
    CHECK_EQ(show(filter(TileFilterKind::Grayscale).apply(red)), "76 76 76 255");
    CHECK_EQ(show(filter(TileFilterKind::Grayscale).apply(Color { 128, 128, 128, 255 })), "128 128 128 255");
    // Dark: white roads go black, and black ones white. The alpha stays.
    CHECK_EQ(show(filter(TileFilterKind::Dark).apply(WHITE)), "0 0 0 255");
    CHECK_EQ(show(filter(TileFilterKind::Dark).apply(Color { 0, 0, 0, 128 })), "255 255 255 128");
    CHECK_EQ(show(filter(TileFilterKind::Tint, Color { 255, 183, 3, 255 }).apply(WHITE)), "255 183 3 255");
}

int main()
{
    test_fit();
    test_fit_in_offset_content();
    test_fit_in_a_circle();
    test_fit_in_a_rounded_rectangle();
    test_follow();
    test_split();
    test_screen_points();
    test_clip_shape();
    test_map_shapes();
    test_tile_level();
    test_map_tiles();
    test_attribution_box();
    test_color_filter();
    return ST::Test::test_result();
}
