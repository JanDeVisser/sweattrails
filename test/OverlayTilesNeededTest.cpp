/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The tiles a renderer asks for before it draws a frame: at the level the
 * spec says for a render and for the preview, only for maps with tiles and
 * a position, and only with tiles to ask. Nothing is drawn, so no window.
 */

#include <format>
#include <set>
#include <string>
#include <vector>

#include <JSON.h>

#include <overlay/OverlayTiles.h>
#include <overlay/Renderer.h>
#include <storage/Storage.h>

#include <TestApp.h>

using namespace ST;

namespace {

constexpr u32 Start = 1'750'000'000;

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

// Tiles that are never there, and downloads that go nowhere: only what's
// asked for matters here.
OverlayTiles no_tiles()
{
    return OverlayTiles {
        [](Tile const &) -> std::expected<std::string, LibCError> { return std::unexpected(LibCError { ENOENT }); },
        [](pTileDownload const &) { },
        TileTextures {
            .load = [](std::string const &) -> std::optional<Texture2D> { return { }; },
            .unload = [](Texture2D const &) { },
        },
    };
}

OverlayLayout layout_of(std::string_view elements)
{
    auto const json = JSONValue::deserialize(std::format(R"({{ "elements": [ {} ] }})", elements));
    auto const layout = json ? decode_layout(*json) : std::unexpected(LayoutError { .message = "not JSON" });
    CHECK_EQ(layout ? std::string { "ok" } : layout.error().to_string(), "ok");
    return layout.value_or(OverlayLayout { });
}

std::optional<fs::path> any_font(std::string_view)
{
    return fs::path { "/nowhere/font.ttf" };
}

FieldValues at_second(OverlaySource const &source, double seconds)
{
    FieldValues ret { .source = &source };
    ret.values[static_cast<size_t>(OverlayField::elapsed)] = seconds;
    return ret;
}

std::set<int> levels(std::vector<Tile> const &tiles)
{
    std::set<int> ret;
    for (auto const &tile : tiles) {
        ret.insert(tile.zoom);
    }
    return ret;
}

// A moment of a minute's video: elements without `visible` show at any.
constexpr VideoTime Always { .at = 1, .length = 60 };

constexpr std::string_view FollowMap = R"({ "type": "map", "view": "follow", "zoom": 15, "size": { "width": 300, "height": 300 },
    "tiles": { "filter": "dark" } })";

Rectangle frame(float width, float height)
{
    return Rectangle { .x = 0, .y = 0, .width = width, .height = height };
}

// A render draws at twice the frame's size, with a detail of 2: level 15
// at 1080p, 16 at 4K, 15 at 1440p. The preview, at its own smaller size,
// asks for a lower level.
void test_levels()
{
    Ride                r;
    OverlaySource const source { r.activity };
    auto                tiles = no_tiles();
    OverlayRenderer     renderer { any_font };
    renderer.set_layout(layout_of(FollowMap), "test.overlay.json");
    CHECK(!renderer.error());

    renderer.set_tiles(&tiles, 2);
    auto const hd = renderer.tiles_needed(at_second(source, 1.5), frame(3840, 2160), Always);
    CHECK_EQ(levels(hd), std::set { 15 });
    // 600 px of the large frame at 512 px a tile: two or three across.
    CHECK(hd.size() >= 4 && hd.size() <= 9);
    CHECK_EQ(levels(renderer.tiles_needed(at_second(source, 1.5), frame(7680, 4320), Always)), std::set { 16 });
    CHECK_EQ(levels(renderer.tiles_needed(at_second(source, 1.5), frame(5120, 2880), Always)), std::set { 15 });

    renderer.set_tiles(&tiles, 1);
    CHECK_EQ(levels(renderer.tiles_needed(at_second(source, 1.5), frame(960, 540), Always)), std::set { 14 });
}

// The same tiles as the frame after, while the rider is in the same place.
void test_follow_moves()
{
    Ride                r;
    OverlaySource const source { r.activity };
    auto                tiles = no_tiles();
    OverlayRenderer     renderer { any_font };
    renderer.set_layout(layout_of(FollowMap), "test.overlay.json");
    renderer.set_tiles(&tiles, 2);
    auto const now = renderer.tiles_needed(at_second(source, 1.5), frame(3840, 2160), Always);
    auto const again = renderer.tiles_needed(at_second(source, 1.5), frame(3840, 2160), Always);
    CHECK(now == again);
    CHECK(!now.empty());
}

// No tiles without a position, for a map without tiles, or without
// anything to ask them of, as in tests.
void test_none()
{
    Ride                r;
    OverlaySource const source { r.activity };
    auto                tiles = no_tiles();
    {
        OverlayRenderer renderer { any_font };
        renderer.set_layout(layout_of(FollowMap), "test.overlay.json");
        CHECK(renderer.tiles_needed(at_second(source, 1.5), frame(3840, 2160), Always).empty());
        renderer.set_tiles(&tiles, 2);
        FieldValues const without_source { };
        CHECK(renderer.tiles_needed(without_source, frame(3840, 2160), Always).empty());
    }
    {
        OverlayRenderer renderer { any_font };
        renderer.set_layout(layout_of(R"({ "type": "map", "view": "follow", "size": { "width": 300, "height": 300 } })"), "test.overlay.json");
        renderer.set_tiles(&tiles, 2);
        CHECK(renderer.tiles_needed(at_second(source, 1.5), frame(3840, 2160), Always).empty());
    }
    Storage             storage { };
    Activity            indoors { storage, ActivityID { } };
    OverlaySource const without { indoors };
    OverlayRenderer     renderer { any_font };
    renderer.set_layout(layout_of(FollowMap), "test.overlay.json");
    renderer.set_tiles(&tiles, 2);
    CHECK(renderer.tiles_needed(at_second(without, 1.5), frame(3840, 2160), Always).empty());
}

// `fit`: the few tiles of the whole ride, the same on every frame.
void test_fit()
{
    Ride                r;
    OverlaySource const source { r.activity };
    auto                tiles = no_tiles();
    OverlayRenderer     renderer { any_font };
    renderer.set_layout(layout_of(R"({ "type": "map", "view": "fit", "size": { "width": 300, "height": 300 }, "tiles": { } })"), "test.overlay.json");
    renderer.set_tiles(&tiles, 2);
    auto const first = renderer.tiles_needed(at_second(source, 0), frame(3840, 2160), Always);
    auto const last = renderer.tiles_needed(at_second(source, 3), frame(3840, 2160), Always);
    CHECK(!first.empty());
    CHECK(first.size() <= 9);
    CHECK(first == last);
    CHECK(levels(first).size() == 1 && *levels(first).begin() <= MaxMapZoom);
}

}

// A map that isn't showing at the moment needs no tiles.
void test_hidden()
{
    Ride                r;
    OverlaySource const source { r.activity };
    auto                tiles = no_tiles();
    OverlayRenderer     renderer { any_font };
    renderer.set_layout(layout_of(R"({ "type": "map", "view": "follow", "size": { "width": 300, "height": 300 },
        "tiles": { }, "visible": { "from": 5, "to": 10 } })"),
        "test.overlay.json");
    renderer.set_tiles(&tiles, 2);
    auto const at = [&](double t) { return renderer.tiles_needed(at_second(source, 1.5), frame(3840, 2160), VideoTime { .at = t, .length = 60 }); };
    CHECK(at(4).empty());
    CHECK(!at(5).empty());
    CHECK(at(10).empty());
}

// A map in a group asks for tiles only while both the group and the map
// show. Not where the group puts it: a map's tiles depend on its size and
// view, not its place on the screen, which test_group_box checks.
void test_in_group()
{
    Ride                r;
    OverlaySource const source { r.activity };
    auto                tiles = no_tiles();
    OverlayRenderer     alone { any_font };
    alone.set_layout(layout_of(FollowMap), "test.overlay.json");
    alone.set_tiles(&tiles, 2);
    auto const expected = alone.tiles_needed(at_second(source, 1.5), frame(3840, 2160), Always);

    OverlayRenderer renderer { any_font };
    renderer.set_layout(layout_of(std::format(R"({{ "type": "group", "size": {{ "width": 600, "height": 600 }}, "padding": 20,
        "at": {{ "x": -40, "y": 40, "anchor": "top-right" }}, "visible": {{ "to": 10 }},
        "children": [ {{ "type": "group", "size": {{ "width": 400, "height": 400 }}, "children": [
            {}, {{ "type": "map", "view": "follow", "zoom": 15, "size": {{ "width": 300, "height": 300 }}, "tiles": {{ }}, "visible": {{ "from": 5 }} }} ] }} ] }})",
                            FollowMap)),
        "test.overlay.json");
    renderer.set_tiles(&tiles, 2);
    auto const at = [&](double t) { return renderer.tiles_needed(at_second(source, 1.5), frame(3840, 2160), VideoTime { .at = t, .length = 60 }); };
    auto const shown = at(1);
    CHECK(!shown.empty());
    CHECK(shown == expected);
    // The second map shows too, from 5 s, and the group's gone at 10.
    CHECK(at(6).size() > shown.size());
    CHECK(at(10).empty());
}

int main()
{
    test_levels();
    test_follow_moves();
    test_none();
    test_fit();
    test_hidden();
    test_in_group();
    return ST::Test::test_result();
}
