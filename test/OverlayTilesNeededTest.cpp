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
    auto const hd = renderer.tiles_needed(at_second(source, 1.5), frame(3840, 2160));
    CHECK_EQ(levels(hd), std::set { 15 });
    // 600 px of the large frame at 512 px a tile: two or three across.
    CHECK(hd.size() >= 4 && hd.size() <= 9);
    CHECK_EQ(levels(renderer.tiles_needed(at_second(source, 1.5), frame(7680, 4320))), std::set { 16 });
    CHECK_EQ(levels(renderer.tiles_needed(at_second(source, 1.5), frame(5120, 2880))), std::set { 15 });

    renderer.set_tiles(&tiles, 1);
    CHECK_EQ(levels(renderer.tiles_needed(at_second(source, 1.5), frame(960, 540))), std::set { 14 });
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
    auto const now = renderer.tiles_needed(at_second(source, 1.5), frame(3840, 2160));
    auto const again = renderer.tiles_needed(at_second(source, 1.5), frame(3840, 2160));
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
        CHECK(renderer.tiles_needed(at_second(source, 1.5), frame(3840, 2160)).empty());
        renderer.set_tiles(&tiles, 2);
        FieldValues const without_source { };
        CHECK(renderer.tiles_needed(without_source, frame(3840, 2160)).empty());
    }
    {
        OverlayRenderer renderer { any_font };
        renderer.set_layout(layout_of(R"({ "type": "map", "view": "follow", "size": { "width": 300, "height": 300 } })"), "test.overlay.json");
        renderer.set_tiles(&tiles, 2);
        CHECK(renderer.tiles_needed(at_second(source, 1.5), frame(3840, 2160)).empty());
    }
    Storage             storage { };
    Activity            indoors { storage, ActivityID { } };
    OverlaySource const without { indoors };
    OverlayRenderer     renderer { any_font };
    renderer.set_layout(layout_of(FollowMap), "test.overlay.json");
    renderer.set_tiles(&tiles, 2);
    CHECK(renderer.tiles_needed(at_second(without, 1.5), frame(3840, 2160)).empty());
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
    auto const first = renderer.tiles_needed(at_second(source, 0), frame(3840, 2160));
    auto const last = renderer.tiles_needed(at_second(source, 3), frame(3840, 2160));
    CHECK(!first.empty());
    CHECK(first.size() <= 9);
    CHECK(first == last);
    CHECK(levels(first).size() == 1 && *levels(first).begin() <= MaxMapZoom);
}

}

int main()
{
    test_levels();
    test_follow_moves();
    test_none();
    test_fit();
    return ST::Test::test_result();
}
