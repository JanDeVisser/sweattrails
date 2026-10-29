/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

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

}

int main()
{
    test_shipped_worlds();
    test_worlds_left_out();
    test_worlds_that_arent_json();
    test_cache_name();
    test_zwift_world();
    return ST::Test::test_result();
}
