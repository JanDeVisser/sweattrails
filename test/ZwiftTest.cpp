/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
#include <filesystem>
#include <format>
#include <print>
#include <string>
#include <vector>

#include <IO.h>

#include <Zwift.h>
#include <storage/Activity.h>
#include <storage/Storage.h>

#include <TempDir.h>
#include <TestApp.h>

using namespace ST;

namespace fs = std::filesystem;

namespace {

fs::path const ShippedWorlds = fs::path { SWEATTRAILS_SOURCE_DIR } / "share" / "zwift-worlds.json";

std::string name_of(ZwiftWorld const *world)
{
    return (world != nullptr) ? world->name : "none";
}

void test_shipped_worlds()
{
    auto const worlds = read_zwift_worlds(ShippedWorlds);
    CHECK(worlds.has_value());
    if (!worlds) {
        return;
    }
    CHECK_EQ(worlds->size(), 12uz);
    auto const &watopia = worlds->front();
    CHECK_EQ(watopia.name, "Watopia");
    CHECK_EQ(watopia.cache_name(), "MiniMap_Watopia_2");
    CHECK_EQ(watopia.north_west.lat, -11.62597f);
    CHECK_EQ(watopia.south_east.lon, 167.03255f);
    auto const box = watopia.box();
    CHECK_EQ(box.north(), -11.62597f);
    CHECK_EQ(box.south(), -11.74087f);
    CHECK_EQ(box.west(), 166.87747f);
    CHECK_EQ(box.east(), 167.03255f);
}

void test_worlds_left_out()
{
    Test::TempDir dir { "zwift" };
    auto const    file = dir.path / "worlds.json";
    MUST(write_file_by_name(file.string(), std::string_view { R"([
        { "name": "Good", "image": "https://x/Good.png", "north_west": [1, 2], "south_east": [0, 3] },
        { "name": "No corner", "image": "https://x/No.png", "north_west": [1, 2] },
        { "name": "Bad corner", "image": "https://x/Bad.png", "north_west": [1], "south_east": [0, 3] },
        "not a world"
    ])" }));
    auto const worlds = read_zwift_worlds(file);
    CHECK(worlds.has_value());
    CHECK_EQ(worlds.value_or(std::vector<ZwiftWorld> { }).size(), 1uz);
}

void test_worlds_that_arent_json()
{
    Test::TempDir dir { "zwift" };
    auto const    file = dir.path / "worlds.json";
    MUST(write_file_by_name(file.string(), std::string_view { "[ { \"name\": " }));
    CHECK(!read_zwift_worlds(file).has_value());
    MUST(write_file_by_name(file.string(), std::string_view { "{ }" }));
    CHECK(!read_zwift_worlds(file).has_value());
    CHECK(!read_zwift_worlds(dir.path / "missing.json").has_value());
}

void test_cache_name()
{
    auto const name = [](std::string url) {
        return ZwiftWorld { .image = std::move(url) }.cache_name();
    };
    CHECK_EQ(name("https://cdn.zwift.com/static/images/maps/MiniMap_Japan.png"), "MiniMap_Japan");
    CHECK_EQ(name("https://cdn.zwift.com/static/images/maps/MiniMap_Watopia_2.png?v=3"), "MiniMap_Watopia_2");
    CHECK_EQ(name("https://example.com/maps/world.v2.png#top"), "world.v2");
    CHECK_EQ(name("Watopia"), "Watopia");
}

struct Ride {
    Storage  storage { };
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };

    Ride(std::optional<FIT::manufacturer> manufacturer, std::optional<Box> track)
    {
        activity.manufacturer = manufacturer;
        activity.segment.box = track;
    }
};

Box box(float32 south, float32 west, float32 north, float32 east)
{
    return Box { .sw = { .lat = south, .lon = west }, .ne = { .lat = north, .lon = east } };
}

void test_zwift_world()
{
    auto const worlds = MUST_EVAL(read_zwift_worlds(ShippedWorlds));
    auto const in_watopia = box(-11.70f, 166.90f, -11.65f, 167.00f);
    auto const in_london = box(51.48f, -0.15f, 51.52f, -0.08f);

    CHECK_EQ(name_of(zwift_world(worlds, Ride { FIT::manufacturer::zwift, in_watopia }.activity)), "Watopia");
    CHECK_EQ(name_of(zwift_world(worlds, Ride { FIT::manufacturer::zwift, in_london }.activity)), "London");
    // An outdoor ride in London isn't in Zwift's.
    CHECK_EQ(name_of(zwift_world(worlds, Ride { FIT::manufacturer::garmin, in_london }.activity)), "none");
    CHECK_EQ(name_of(zwift_world(worlds, Ride { std::nullopt, in_watopia }.activity)), "none");
    // Past Watopia's eastern edge.
    CHECK_EQ(name_of(zwift_world(worlds, Ride { FIT::manufacturer::zwift, box(-11.70f, 166.90f, -11.65f, 167.10f) }.activity)), "none");
    // Indoors, without a position.
    CHECK_EQ(name_of(zwift_world(worlds, Ride { FIT::manufacturer::zwift, std::nullopt }.activity)), "none");
}

// Watopia's image, 8192x6144.
constexpr Vector2 WatopiaImage { .x = 8192, .y = 6144 };

void test_native_zoom()
{
    auto const worlds = MUST_EVAL(read_zwift_worlds(ShippedWorlds));
    CHECK_EQ(native_zoom(worlds.front(), WatopiaImage.x), 16);
    // Half the pixels is a level less.
    CHECK_EQ(native_zoom(worlds.front(), WatopiaImage.x / 2), 15);
    CHECK_EQ(native_zoom(worlds.front(), 1), MinWorldZoom);
    CHECK_EQ(native_zoom(worlds.front(), 0), MinWorldZoom);
    CHECK_EQ(native_zoom(worlds.front(), 1e9f), MaxMapZoom);
}

bool whole(float f)
{
    return std::floor(f) == f;
}

void test_world_tiles()
{
    auto const  worlds = MUST_EVAL(read_zwift_worlds(ShippedWorlds));
    auto const &watopia = worlds.front();
    for (auto zoom = MinWorldZoom; zoom <= 16; ++zoom) {
        auto const area = world_area(watopia, zoom);
        auto const nw = world_pixel(watopia.north_west, zoom);
        auto const se = world_pixel(watopia.south_east, zoom);
        // The world's place on the map, to the nearest pixel.
        CHECK(std::abs(static_cast<double>(area.left) - nw.x) <= 0.5);
        CHECK(std::abs(static_cast<double>(area.width) - (se.x - nw.x)) <= 1.0);
        CHECK(std::abs(static_cast<double>(area.height) - (se.y - nw.y)) <= 1.0);

        auto const tiles = world_tiles(watopia, zoom);
        CHECK(!tiles.empty());
        if (tiles.empty()) {
            continue;
        }
        // From the tile with the world's top left pixel to the one with its
        // bottom right pixel.
        CHECK_EQ(tiles.front().tile, (Tile { .x = static_cast<u32>(area.left / 256), .y = static_cast<u32>(area.top / 256), .zoom = zoom }));
        CHECK_EQ(tiles.back().tile, (Tile { .x = static_cast<u32>((area.left + area.width - 1) / 256), .y = static_cast<u32>((area.top + area.height - 1) / 256), .zoom = zoom }));
        auto const columns = tiles.back().tile.x - tiles.front().tile.x + 1;
        auto const rows = tiles.back().tile.y - tiles.front().tile.y + 1;
        CHECK_EQ(tiles.size(), static_cast<size_t>(columns * rows));

        i64 pixels = 0;
        for (auto const &[tile, source, place] : tiles) {
            CHECK(tile.zoom == zoom);
            CHECK(whole(source.x) && whole(source.y) && whole(place.x) && whole(place.y) && whole(place.width) && whole(place.height));
            // One image pixel to a map pixel.
            CHECK(source.width == place.width && source.height == place.height);
            CHECK(place.width > 0 && place.height > 0);
            CHECK(place.x >= 0 && place.y >= 0 && place.x + place.width <= 256 && place.y + place.height <= 256);
            CHECK(source.x >= 0 && source.y >= 0 && source.x + source.width <= area.width && source.y + source.height <= area.height);
            // Where it is in the tile is where it is in the world.
            CHECK_EQ(static_cast<i64>(tile.x) * 256 + static_cast<i64>(place.x), area.left + static_cast<i64>(source.x));
            CHECK_EQ(static_cast<i64>(tile.y) * 256 + static_cast<i64>(place.y), area.top + static_cast<i64>(source.y));
            pixels += static_cast<i64>(source.width) * static_cast<i64>(source.height);
        }
        // Each pixel of the image is in one tile.
        CHECK_EQ(pixels, static_cast<i64>(area.width) * area.height);
    }
}

void test_world_tiles_at_16()
{
    auto const worlds = MUST_EVAL(read_zwift_worlds(ShippedWorlds));
    auto const tiles = world_tiles(worlds.front(), 16);
    // The first tile shows the image's top left corner, in its bottom right.
    auto const &first = tiles.front();
    CHECK_EQ(first.source.x, 0.0f);
    CHECK_EQ(first.source.y, 0.0f);
    CHECK(first.place.x > 0 && first.place.y > 0);
    CHECK_EQ(first.place.x + first.place.width, 256.0f);
    // One in the middle shows a whole tile of the image.
    auto const  columns = tiles.back().tile.x - first.tile.x + 1;
    auto const  rows = tiles.back().tile.y - first.tile.y + 1;
    auto const &middle = tiles[(rows / 2) * columns + columns / 2];
    CHECK_EQ(middle.place.x, 0.0f);
    CHECK_EQ(middle.place.width, 256.0f);
    CHECK_EQ(middle.place.height, 256.0f);
    // Some 600 tiles.
    CHECK(tiles.size() > 400 && tiles.size() < 800);
}

void test_world_tiles_of_nothing()
{
    auto const flat = ZwiftWorld { .north_west = { .lat = 1, .lon = 2 }, .south_east = { .lat = 1, .lon = 3 } };
    CHECK(world_tiles(flat, 10).empty());
}

// Watopia's corners, with an image of 64x48 already in the cache, so that
// nothing is downloaded: red, but for its top left pixel, the sea.
struct SmallWorld {
    Test::TempDir dir { "zwift-cut" };
    ZwiftWorld    world {
        .name = "Small",
        .image = "https://example.invalid/maps/Small.png",
        .north_west = { .lat = -11.62597f, .lon = 166.87747f },
        .south_east = { .lat = -11.74087f, .lon = 167.03255f },
    };

    SmallWorld()
    {
        auto image = GenImageColor(64, 48, RED);
        ImageDrawPixel(&image, 0, 0, BLUE);
        CHECK(ExportImage(image, (dir.path / "Small.png").c_str()));
        UnloadImage(image);
    }
};

bool same(Color a, Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

void test_cut_a_world()
{
    SmallWorld small;
    CHECK(!cut_world(small.dir.path, small.world).has_value());
    auto const map = prepare_zwift_world(small.dir.path, small.world);
    CHECK(map.has_value());
    if (!map) {
        std::println(stderr, "{}", map.error().to_string());
        return;
    }
    // 64 pixels over Watopia's 0.155° is about zoom 9.
    CHECK_EQ(map->native_zoom, 9);
    CHECK(same(map->background, BLUE));
    CHECK_EQ(map->dir.string(), (small.dir.path / "Small").string());

    auto const source = world_source(*map);
    CHECK_EQ(source.dir.string(), map->dir.string());
    CHECK_EQ(std::string { source.attribution }, "Map © Zwift");
    CHECK(!source.downloads);
    CHECK_EQ(source.max_zoom, 9);
    CHECK(same(source.background, BLUE));

    auto const again = cut_world(small.dir.path, small.world);
    CHECK(again.has_value());
    CHECK_EQ(again.transform([](WorldMap const &m) { return m.native_zoom; }).value_or(0), 9);

    for (auto zoom = MinWorldZoom; zoom <= 9; ++zoom) {
        for (auto const &[tile, source, place] : world_tiles(small.world, zoom)) {
            auto const png = tile.get_cached_tile(map->dir);
            CHECK(png.has_value());
            if (!png) {
                continue;
            }
            auto image = LoadImageFromMemory(".png", reinterpret_cast<unsigned char const *>(png->data()), static_cast<int>(png->size()));
            CHECK_EQ(image.width, 256);
            CHECK_EQ(image.height, 256);
            // The world's top left corner is inside its first tile: left of
            // and above it is the sea, and in the middle of the image's part,
            // the image.
            if (place.x >= 1 && place.y >= 1) {
                CHECK(same(GetImageColor(image, 0, 0), BLUE));
            }
            auto const mid_x = static_cast<int>(place.x + place.width / 2);
            auto const mid_y = static_cast<int>(place.y + place.height / 2);
            if (place.width >= 4 && place.height >= 4) {
                CHECK(same(GetImageColor(image, mid_x, mid_y), RED));
            }
            UnloadImage(image);
        }
    }
}

void test_cut_a_world_that_doesnt_decode()
{
    SmallWorld small;
    MUST(write_file_by_name((small.dir.path / "Small.png").string(), std::string_view { "<html>Not found</html>" }));
    CHECK(!prepare_zwift_world(small.dir.path, small.world).has_value());
    // It's not kept, so that the next try downloads it again.
    CHECK(!fs::exists(small.dir.path / "Small.png"));
    CHECK(!cut_world(small.dir.path, small.world).has_value());
}

void test_cut_world_with_a_bad_world_file()
{
    SmallWorld small;
    MUST(prepare_zwift_world(small.dir.path, small.world));
    auto const file = (small.dir.path / "Small" / "world.json").string();
    MUST(write_file_by_name(file, std::string_view { R"({ "native_zoom": 30, "background": "#0000ffff" })" }));
    CHECK(!cut_world(small.dir.path, small.world).has_value());
    MUST(write_file_by_name(file, std::string_view { R"({ "native_zoom": 9, "background": "blue" })" }));
    CHECK(!cut_world(small.dir.path, small.world).has_value());
    MUST(write_file_by_name(file, std::string_view { "{" }));
    CHECK(!cut_world(small.dir.path, small.world).has_value());
}

// Each of a world image's pixel columns ends up in one tile, where it lies
// on the map: none is dropped, or doubled, at a seam between two tiles. The
// image is as large as the world is at zoom 15, its native zoom, so that
// the cut doesn't scale it, and each column has its own color.
void test_no_column_is_lost_at_a_seam()
{
    Test::TempDir dir { "zwift-seams" };
    ZwiftWorld    world {
        .name = "Seams",
        .image = "https://example.invalid/maps/Seams.png",
        .north_west = { .lat = -11.62597f, .lon = 166.87747f },
        .south_east = { .lat = -11.74087f, .lon = 167.03255f },
    };
    u8 const   zoom = 15;
    auto const area = world_area(world, zoom);
    CHECK_EQ(native_zoom(world, static_cast<float>(area.width)), zoom);
    auto const column_color = [](int column) {
        return Color { .r = static_cast<unsigned char>(column % 256), .g = static_cast<unsigned char>(column / 256), .b = 0, .a = 255 };
    };
    auto image = GenImageColor(area.width, area.height, WHITE);
    for (auto column = 0; column < area.width; ++column) {
        ImageDrawRectangle(&image, column, 0, 1, area.height, column_color(column));
    }
    CHECK(ExportImage(image, (dir.path / "Seams.png").c_str()));
    UnloadImage(image);

    auto const map = prepare_zwift_world(dir.path, world);
    CHECK(map.has_value());
    if (!map) {
        return;
    }
    // A row of the image in the middle of the world, across all its tiles.
    auto const y = area.top + area.height / 2;
    auto const row = static_cast<u32>(y / 256);
    auto       lost = 0;
    for (auto tile_x = static_cast<u32>(area.left / 256); static_cast<i64>(tile_x) * 256 < area.left + area.width; ++tile_x) {
        auto const png = Tile { .x = tile_x, .y = row, .zoom = zoom }.get_cached_tile(map->dir);
        CHECK(png.has_value());
        if (!png) {
            continue;
        }
        auto tile = LoadImageFromMemory(".png", reinterpret_cast<unsigned char const *>(png->data()), static_cast<int>(png->size()));
        for (auto px = 0; px < 256; ++px) {
            auto const column = static_cast<i64>(tile_x) * 256 + px - area.left;
            if (column < 0 || column >= area.width) {
                continue;
            }
            auto const want = column_color(static_cast<int>(column));
            if (!same(GetImageColor(tile, px, static_cast<int>(y % 256)), want)) {
                ++lost;
            }
        }
        UnloadImage(tile);
    }
    CHECK_EQ(lost, 0);
}

// A cut that was interrupted, with tiles but no world.json, isn't taken for
// a world's map, and is done again.
void test_interrupted_cut_is_done_again()
{
    SmallWorld small;
    MUST(prepare_zwift_world(small.dir.path, small.world));
    auto const file = small.dir.path / "Small" / "world.json";
    fs::remove(file);
    CHECK(!cut_world(small.dir.path, small.world).has_value());
    CHECK(prepare_zwift_world(small.dir.path, small.world).has_value());
    CHECK(fs::exists(file));
    CHECK(cut_world(small.dir.path, small.world).has_value());
}

// A world whose corners lie on tiles' edges touches only the tiles inside.
void test_world_tiles_on_tile_edges()
{
    // At zoom 10, longitude 0 is x = 512 * 256, and each tile is 360/1024°.
    auto const tile_lon = 360.0f / 1024.0f;
    auto const world = ZwiftWorld {
        .north_west = { .lat = 10.0f, .lon = 0.0f },
        .south_east = { .lat = 9.5f, .lon = 2 * tile_lon },
    };
    auto const tiles = world_tiles(world, 10);
    for (auto const &[tile, source, place] : tiles) {
        CHECK(tile.x == 512 || tile.x == 513);
        CHECK(place.width > 0.5f);
    }
}

}

int main()
{
    // raylib logs each image it saves and loads.
    SetTraceLogLevel(LOG_WARNING);
    test_shipped_worlds();
    test_worlds_left_out();
    test_worlds_that_arent_json();
    test_cache_name();
    test_zwift_world();
    test_native_zoom();
    test_world_tiles();
    test_world_tiles_at_16();
    test_world_tiles_of_nothing();
    test_cut_a_world();
    test_cut_a_world_that_doesnt_decode();
    test_cut_world_with_a_bad_world_file();
    test_no_column_is_lost_at_a_seam();
    test_interrupted_cut_is_done_again();
    test_world_tiles_on_tile_edges();
    return ST::Test::test_result();
}
