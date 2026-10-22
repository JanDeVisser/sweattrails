/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the map code that needs neither a window nor the network: the grid
 * of tiles Map::init() picks for a route, and fetch_tiles(), the loop the
 * job thread runs to download the tiles a map is missing. A fake fetcher
 * stands in for the download.
 */

#include <expected>
#include <string>
#include <vector>

#include <Error.h>

#include <Coordinates.h>
#include <Map.h>

#include <TestApp.h>

using namespace ST;

namespace {

// A download of `count` tiles, with index i for the tile at x = i.
pTileDownload make_download(size_t count)
{
    auto download = std::make_shared<TileDownload>();
    for (size_t ix = 0; ix < count; ++ix) {
        download->tiles.push_back(MissingTile {
            .index = ix,
            .tile = Tile { .x = static_cast<u32>(ix), .y = 0, .zoom = 10 },
        });
    }
    return download;
}

std::string png_for(Tile const &tile)
{
    return std::format("png {}", tile.x);
}

struct Received {
    size_t      index;
    std::string png;
};

struct Fetch {
    std::vector<u32>      fetched { };
    std::vector<Received> received { };

    size_t run(TileDownload const &download, TileFetcher const &fetch)
    {
        return fetch_tiles(
            download,
            [this, &fetch](Tile const &tile) {
                fetched.push_back(tile.x);
                return fetch(tile);
            },
            [this](size_t index, std::string png) {
                received.push_back(Received { index, std::move(png) });
            });
    }
};

std::expected<std::string, LibCError> fetch_ok(Tile const &tile)
{
    return png_for(tile);
}

void test_all_tiles_arrive()
{
    auto  download = make_download(3);
    Fetch f;
    auto  failed = f.run(*download, fetch_ok);
    CHECK_EQ(failed, 0uz);
    CHECK_EQ(f.received.size(), 3uz);
    for (size_t ix = 0; ix < f.received.size(); ++ix) {
        CHECK_EQ(f.received[ix].index, ix);
        CHECK_EQ(f.received[ix].png, png_for(download->tiles[ix].tile));
    }
}

void test_failed_tiles_are_counted_and_not_sent()
{
    auto  download = make_download(4);
    Fetch f;
    auto  failed = f.run(*download, [](Tile const &tile) -> std::expected<std::string, LibCError> {
        if (tile.x == 1 || tile.x == 2) {
            return std::unexpected(LibCError { "no network" });
        }
        return png_for(tile);
    });
    CHECK_EQ(failed, 2uz);
    CHECK_EQ(f.fetched.size(), 4uz);
    CHECK_EQ(f.received.size(), 2uz);
    if (f.received.size() == 2) {
        CHECK_EQ(f.received[0].index, 0uz);
        CHECK_EQ(f.received[1].index, 3uz);
    }
}

void test_nothing_is_fetched_when_cancelled()
{
    auto download = make_download(3);
    download->cancelled = true;
    Fetch f;
    auto  failed = f.run(*download, fetch_ok);
    CHECK_EQ(failed, 0uz);
    CHECK(f.fetched.empty());
    CHECK(f.received.empty());
}

// Opening another activity while a tile downloads: the tile finishes, but
// isn't sent, and the tiles after it aren't fetched.
void test_cancel_while_fetching()
{
    auto  download = make_download(4);
    Fetch f;
    auto  failed = f.run(*download, [&download](Tile const &tile) -> std::expected<std::string, LibCError> {
        if (tile.x == 1) {
            download->cancelled = true;
        }
        return png_for(tile);
    });
    CHECK_EQ(failed, 0uz);
    CHECK_EQ(f.fetched.size(), 2uz);
    CHECK_EQ(f.received.size(), 1uz);
    if (!f.received.empty()) {
        CHECK_EQ(f.received[0].index, 0uz);
    }
}

// A ride around Toronto's waterfront, on a map of 600x400 pixels.
Box const route { .sw = { .lat = 43.62f, .lon = -79.45f }, .ne = { .lat = 43.67f, .lon = -79.35f } };

void test_map_grid_covers_route()
{
    auto map = Map::init(route, 600, 400);
    CHECK(map.has_value());
    if (!map) {
        return;
    }
    CHECK(map->zoom > 0 && map->zoom <= 16);
    CHECK_EQ(map->num_tiles, map->columns * map->rows);
    CHECK_EQ(map->tile(0), (Tile { .x = map->x, .y = map->y, .zoom = map->zoom }));

    auto const last = map->tile(map->num_tiles - 1);
    CHECK_EQ(last, (Tile { .x = map->x + map->columns - 1, .y = map->y + map->rows - 1, .zoom = map->zoom }));

    auto const first = map->tile(0);
    CHECK(first.north() >= route.north());
    CHECK(first.west() <= route.west());
    CHECK(last.south() <= route.south());
    CHECK(last.east() >= route.east());
}

void test_map_without_zoom_level()
{
    Box const world { .sw = { .lat = -80.0f, .lon = -179.0f }, .ne = { .lat = 80.0f, .lon = 179.0f } };
    CHECK(!Map::init(world, 100, 100).has_value());
}

}

int main()
{
    test_all_tiles_arrive();
    test_failed_tiles_are_counted_and_not_sent();
    test_nothing_is_fetched_when_cancelled();
    test_cancel_while_fetching();
    test_map_grid_covers_route();
    test_map_without_zoom_level();
    return ST::Test::test_result();
}
