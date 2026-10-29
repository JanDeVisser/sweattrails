/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
#include <filesystem>
#include <format>
#include <string>

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

bool near(double a, double b)
{
    return std::abs(a - b) < 0.01;
}

void test_world_tiles()
{
    auto const  worlds = MUST_EVAL(read_zwift_worlds(ShippedWorlds));
    auto const &watopia = worlds.front();
    for (auto zoom = MinWorldZoom; zoom <= 16; ++zoom) {
        auto const tiles = world_tiles(watopia, WatopiaImage, zoom);
        CHECK(!tiles.empty());
        auto const nw = world_pixel(watopia.north_west, zoom);
        auto const se = world_pixel(watopia.south_east, zoom);
        // The tiles from the one holding the north-west corner to the one
        // holding the south-east corner.
        CHECK_EQ(tiles.front().tile, Tile::for_coordinates(watopia.north_west, zoom));
        CHECK_EQ(tiles.back().tile, Tile::for_coordinates(watopia.south_east, zoom));
        auto const columns = tiles.back().tile.x - tiles.front().tile.x + 1;
        auto const rows = tiles.back().tile.y - tiles.front().tile.y + 1;
        CHECK_EQ(tiles.size(), static_cast<size_t>(columns * rows));

        double image_area = 0;
        double map_area = 0;
        for (auto const &[tile, source, place] : tiles) {
            CHECK(tile.zoom == zoom);
            CHECK(place.x >= 0 && place.y >= 0 && place.x + place.width <= 256.001f && place.y + place.height <= 256.001f);
            CHECK(source.x >= -0.001f && source.y >= -0.001f);
            CHECK(source.x + source.width <= WatopiaImage.x + 0.01f && source.y + source.height <= WatopiaImage.y + 0.01f);
            // The image is stretched evenly.
            CHECK(near(source.width / place.width, WatopiaImage.x / (se.x - nw.x)));
            CHECK(near(source.height / place.height, WatopiaImage.y / (se.y - nw.y)));
            image_area += static_cast<double>(source.width) * source.height;
            map_area += static_cast<double>(place.width) * place.height;
        }
        // Each pixel of the image is in one tile, to a float's precision.
        auto const whole_image = static_cast<double>(WatopiaImage.x) * WatopiaImage.y;
        auto const whole_map = (se.x - nw.x) * (se.y - nw.y);
        CHECK(std::abs(image_area - whole_image) < whole_image * 1e-6);
        CHECK(std::abs(map_area - whole_map) < whole_map * 1e-6);
    }
}

void test_world_tiles_at_16()
{
    auto const worlds = MUST_EVAL(read_zwift_worlds(ShippedWorlds));
    auto const tiles = world_tiles(worlds.front(), WatopiaImage, 16);
    // The first tile shows the image's top left corner, in its bottom right.
    auto const &first = tiles.front();
    CHECK_EQ(first.source.x, 0.0f);
    CHECK_EQ(first.source.y, 0.0f);
    CHECK(first.place.x > 0 && first.place.y > 0);
    CHECK(near(first.place.x + first.place.width, 256));
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
    CHECK(world_tiles(flat, WatopiaImage, 10).empty());
}

}

int main()
{
    test_shipped_worlds();
    test_worlds_left_out();
    test_worlds_that_arent_json();
    test_cache_name();
    test_zwift_world();
    test_native_zoom();
    test_world_tiles();
    test_world_tiles_at_16();
    test_world_tiles_of_nothing();
    return ST::Test::test_result();
}
