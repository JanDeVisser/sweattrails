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

#include <chrono>
#include <cmath>
#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <Error.h>

#include <Coordinates.h>
#include <Map.h>
#include <storage/Storage.h>

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

void test_empty_download()
{
    auto  download = make_download(0);
    Fetch f;
    auto  failed = f.run(*download, fetch_ok);
    CHECK_EQ(failed, 0uz);
    CHECK(f.fetched.empty());
    CHECK(f.received.empty());
}

// The last tile is being fetched when the download is cancelled: nothing
// is sent for it, and it isn't a failure.
void test_cancel_while_fetching_last_tile()
{
    auto  download = make_download(3);
    Fetch f;
    auto  failed = f.run(*download, [&download](Tile const &tile) -> std::expected<std::string, LibCError> {
        if (tile.x == 2) {
            download->cancelled = true;
            return std::unexpected(LibCError { "no network" });
        }
        return png_for(tile);
    });
    CHECK_EQ(failed, 0uz);
    CHECK_EQ(f.fetched.size(), 3uz);
    CHECK_EQ(f.received.size(), 2uz);
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

// ActivityMap draws tile `ix` at column ix % columns and row ix / columns
// of its image, so Map::tile(ix) has to be the tile at that place.
void test_tile_index_matches_image_position()
{
    for (auto const [width, height] : { std::pair<u16, u16> { 600, 400 }, { 300, 700 }, { 1000, 250 } }) {
        auto map = Map::init(route, width, height);
        CHECK(map.has_value());
        if (!map) {
            continue;
        }
        CHECK(map->columns > 0 && map->rows > 0);
        CHECK_EQ(map->num_tiles, map->columns * map->rows);
        for (size_t ix = 0; ix < map->num_tiles; ++ix) {
            auto const column = static_cast<u32>(ix % map->columns);
            auto const row = static_cast<u32>(ix / map->columns);
            CHECK_EQ(map->tile(ix), (Tile { .x = map->x + column, .y = map->y + row, .zoom = map->zoom }));
        }
        // The image is columns x rows tiles, and has to cover the visible
        // rectangle.
        CHECK(map->rectangle.x >= 0 && map->rectangle.y >= 0);
        CHECK(map->rectangle.x + map->rectangle.width <= map->columns * 256.0f);
        CHECK(map->rectangle.y + map->rectangle.height <= map->rows * 256.0f);
    }
}

// A cache in a fresh temporary directory, removed again when it goes.
struct TileCache {
    Storage storage { };

    TileCache()
    {
        storage.tiles_dir = std::filesystem::temp_directory_path() / std::format("map_test_tiles_{}", std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(storage.tiles_dir);
    }

    ~TileCache()
    {
        std::error_code ec { };
        std::filesystem::remove_all(storage.tiles_dir, ec);
    }

    void put(Tile const &tile, std::string const &png) const
    {
        auto const dir = storage.tiles_dir / std::format("{}/{}", tile.zoom, tile.x);
        std::filesystem::create_directories(dir);
        std::ofstream(dir / std::format("{}.png", tile.y), std::ios::binary) << png;
    }
};

// The UI thread reads the cache with get_cached_tile(), and only the
// missing tiles go to the job.
void test_cached_tile()
{
    TileCache  cache;
    Tile const cached { .x = 4577, .y = 5980, .zoom = 14 };
    Tile const missing { .x = 4578, .y = 5980, .zoom = 14 };
    cache.put(cached, "cached png");

    auto const hit = cached.get_cached_tile(cache.storage);
    CHECK(hit.has_value());
    if (hit) {
        CHECK_EQ(*hit, "cached png");
    }
    CHECK(!missing.get_cached_tile(cache.storage).has_value());
}

// The job fetches with get_tile(). A tile that a cancelled download
// finished caching in the meantime is taken from the cache, not
// downloaded again.
void test_get_tile_prefers_cache()
{
    TileCache  cache;
    Tile const tile { .x = 4577, .y = 5980, .zoom = 14 };
    cache.put(tile, "cached png");
    auto const png = tile.get_tile(cache.storage);
    CHECK(png.has_value());
    if (png) {
        CHECK_EQ(*png, "cached png");
    }
}

void test_map_without_zoom_level()
{
    Box const world { .sw = { .lat = -80.0f, .lon = -179.0f }, .ne = { .lat = 80.0f, .lon = 179.0f } };
    CHECK(!Map::init(world, 100, 100).has_value());
}

bool near(double a, double b, double tolerance = 1e-6)
{
    return std::abs(a - b) <= tolerance;
}

bool near(Vector2 a, Vector2 b, float tolerance = 1e-3f)
{
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance;
}

// world_pixel() is the projection Tile::for_coordinates() does, in pixels.
void test_world_pixel()
{
    Coordinates const toronto { .lat = 43.65f, .lon = -79.38f };
    for (u8 zoom : { 1, 10, 16, 18 }) {
        auto const p = world_pixel(toronto, zoom);
        auto const tile = Tile::for_coordinates(toronto, zoom);
        CHECK_EQ(static_cast<u32>(p.x / 256), tile.x);
        CHECK_EQ(static_cast<u32>(p.y / 256), tile.y);
        auto const back = world_coordinates(p, zoom);
        CHECK(near(back.lat, toronto.lat, 1e-4) && near(back.lon, toronto.lon, 1e-4));
    }
    CHECK(near(world_pixel(Coordinates { }, 0).x, 128.0));
    CHECK(near(world_pixel(Coordinates { }, 0).y, 128.0));
}

// The zoom MapView::fit() picks is the one Map::init() picked.
void test_fit_covers_route()
{
    for (auto const [width, height] : { std::pair<u16, u16> { 600, 400 }, { 300, 700 }, { 1000, 250 } }) {
        auto const view = MapView::fit(route, width, height);
        auto const map = Map::init(route, width, height);
        CHECK(view.has_value() && map.has_value());
        if (!view || !map) {
            continue;
        }
        CHECK_EQ(view->zoom, map->zoom);
        auto const nw = view->to_screen(world_pixel(Coordinates { .lat = route.north(), .lon = route.west() }, view->zoom));
        auto const se = view->to_screen(world_pixel(Coordinates { .lat = route.south(), .lon = route.east() }, view->zoom));
        CHECK(nw.x >= 0 && nw.y >= 0);
        CHECK(se.x <= width && se.y <= height);
        // Centered.
        CHECK(near(nw.x, width - se.x, 1e-2) && near(nw.y, height - se.y, 1e-2));
    }
    Box const world { .sw = { .lat = -80.0f, .lon = -179.0f }, .ne = { .lat = 80.0f, .lon = 179.0f } };
    CHECK(!MapView::fit(world, 100, 100).has_value());
}

void test_screen_and_world()
{
    MapView const view { .zoom = 18, .center = { .x = 40'000'000.5, .y = 30'000'000.25 }, .width = 600, .height = 400 };
    CHECK(near(view.to_screen(view.center), Vector2 { .x = 300, .y = 200 }));
    // To the pixel at zoom 18, where a float wouldn't do.
    auto const p = view.to_world(Vector2 { .x = 1, .y = 399 });
    CHECK(near(p.x, 39'999'701.5) && near(p.y, 30'000'199.25));
    CHECK(near(view.to_screen(p), Vector2 { .x = 1, .y = 399 }));
}

void test_zoom_keeps_point_under_mouse()
{
    MapView const view { .zoom = 12, .center = { .x = 300'000, .y = 400'000 }, .width = 600, .height = 400 };
    Vector2 const mouse { .x = 100, .y = 350 };
    auto const    under = world_coordinates(view.to_world(mouse), view.zoom);
    for (auto steps : { 1, 3, -1, -2 }) {
        auto const zoomed = view.zoomed(steps, mouse);
        CHECK_EQ(static_cast<int>(zoomed.zoom), 12 + steps);
        auto const still = world_coordinates(zoomed.to_world(mouse), zoomed.zoom);
        CHECK(near(still.lat, under.lat, 1e-5) && near(still.lon, under.lon, 1e-5));
    }
    // Around the middle, the center stays the same place.
    auto const in = view.zoomed(1, Vector2 { .x = 300, .y = 200 });
    CHECK(near(in.center.x, 600'000) && near(in.center.y, 800'000));
}

void test_pan_and_resize()
{
    MapView const view { .zoom = 10, .center = { .x = 1000, .y = 2000 }, .width = 600, .height = 400 };
    // Dragged right and down: the map moves with the mouse.
    auto const panned = view.panned(Vector2 { .x = 30, .y = 40 });
    CHECK(near(panned.center.x, 970) && near(panned.center.y, 1960));
    auto const resized = view.resized(800, 300);
    CHECK(resized.center == view.center);
    CHECK_EQ(resized.width, 800.0f);
}

void test_tiles_cover_view()
{
    MapView const view { .zoom = 10, .center = { .x = 1000, .y = 2000 }, .width = 600, .height = 400 };
    auto const    tiles = view.tiles();
    // x from 700 to 1300: tiles 2 to 5; y from 1800 to 2200: tiles 7 and 8.
    CHECK_EQ(tiles.size(), 8uz);
    if (!tiles.empty()) {
        CHECK_EQ(tiles.front().tile, (Tile { .x = 2, .y = 7, .zoom = 10 }));
        CHECK_EQ(tiles.front().screen.x, -188.0f);
        CHECK_EQ(tiles.front().screen.y, -8.0f);
        CHECK_EQ(tiles.back().tile, (Tile { .x = 5, .y = 8, .zoom = 10 }));
    }
    // At the world's top left corner, only the tiles in the world.
    MapView const corner { .zoom = 2, .center = { .x = 0, .y = 0 }, .width = 600, .height = 400 };
    auto const    in_world = corner.tiles();
    CHECK_EQ(in_world.size(), 2uz); // x 0..300: tiles 0, 1; y 0..200: tile 0
    for (auto const &placed : in_world) {
        CHECK(placed.screen.x >= 300 && placed.screen.y >= 200);
    }
}

void test_cover_with_ancestors()
{
    Tile const tile { .x = 13, .y = 6, .zoom = 10 };
    // The grandparent, x 3 and y 1 at zoom 8: the tile is its quarter at
    // column 1, row 2.
    auto const grandparent = Tile { .x = 3, .y = 1, .zoom = 8 };
    auto       pieces = cover(tile, [&](Tile const &t) { return t == grandparent; });
    CHECK_EQ(pieces.size(), 1uz);
    if (!pieces.empty()) {
        CHECK_EQ(pieces[0].source, grandparent);
        CHECK_EQ(pieces[0].source_area.x, 64.0f);
        CHECK_EQ(pieces[0].source_area.y, 128.0f);
        CHECK_EQ(pieces[0].source_area.width, 64.0f);
        CHECK_EQ(pieces[0].destination.width, 256.0f);
    }
    // The nearest first.
    auto const parent = Tile { .x = 6, .y = 3, .zoom = 9 };
    pieces = cover(tile, [&](Tile const &t) { return t == parent || t == grandparent; });
    CHECK(pieces.size() == 1 && pieces[0].source == parent);
    // Three levels up, not four.
    CHECK_EQ(cover(tile, [](Tile const &t) { return t.zoom == 7; }).size(), 1uz);
    CHECK(cover(tile, [](Tile const &t) { return t.zoom == 6; }).empty());
    CHECK(cover(tile, [](Tile const &) { return false; }).empty());
}

void test_cover_with_children()
{
    Tile const tile { .x = 5, .y = 2, .zoom = 4 };
    auto const pieces = cover(tile, [](Tile const &t) { return t.zoom == 5 && t.x == 11; });
    // The children are 10 and 11 by 4 and 5: two of them at x 11.
    CHECK_EQ(pieces.size(), 2uz);
    for (auto const &piece : pieces) {
        CHECK_EQ(piece.destination.x, 128.0f);
        CHECK_EQ(piece.destination.width, 128.0f);
    }
}

Rectangle const MapArea { .x = 50, .y = 20, .width = 600, .height = 400 };

ScrollInput mouse_at(float x, float y)
{
    return ScrollInput { .mouse = { .x = x, .y = y } };
}

MapNavigator started()
{
    MapNavigator navigator;
    CHECK(navigator.start(route, MapArea.width, MapArea.height));
    return navigator;
}

void test_navigator_wheel_zooms_around_mouse()
{
    auto       navigator = started();
    auto const fit = navigator.view.zoom;
    auto       input = mouse_at(200, 100);
    auto const under = world_coordinates(navigator.view.to_world(Vector2 { .x = 150, .y = 80 }), fit);
    input.wheel = 1;
    CHECK(navigator.handle(input, MapArea));
    CHECK_EQ(navigator.view.zoom, static_cast<u8>(fit + 1));
    CHECK(navigator.moved);
    auto const still = world_coordinates(navigator.view.to_world(Vector2 { .x = 150, .y = 80 }), navigator.view.zoom);
    CHECK(near(still.lat, under.lat, 1e-5) && near(still.lon, under.lon, 1e-5));
    // Trackpad fractions add up.
    input.wheel = -0.5f;
    CHECK(!navigator.handle(input, MapArea));
    CHECK(navigator.handle(input, MapArea));
    CHECK_EQ(navigator.view.zoom, fit);
    // Not over the map.
    auto elsewhere = mouse_at(700, 100);
    elsewhere.wheel = 1;
    CHECK(!navigator.handle(elsewhere, MapArea));
    CHECK_EQ(navigator.view.zoom, fit);
}

void test_navigator_zoom_limits()
{
    auto       navigator = started();
    auto const fit = navigator.view.zoom;
    navigator.zoom_by(-10);
    CHECK_EQ(navigator.view.zoom, static_cast<u8>(fit - ZoomOutPastFit));
    CHECK(!navigator.zoom_by(-1));
    navigator.zoom_by(20);
    CHECK_EQ(navigator.view.zoom, MaxMapZoom);
    CHECK(!navigator.zoom_by(1));
}

void test_navigator_drag_pans()
{
    auto navigator = started();
    navigator.zoom_by(2);
    auto const before = navigator.view.center;
    auto       press = mouse_at(300, 200);
    press.pressed = true;
    press.down = true;
    CHECK(!navigator.handle(press, MapArea));
    CHECK(navigator.dragging);
    auto drag = mouse_at(310, 195);
    drag.down = true;
    CHECK(navigator.handle(drag, MapArea));
    CHECK(near(navigator.view.center.x, before.x - 10) && near(navigator.view.center.y, before.y + 5));
    // Outside the map, still dragging.
    drag.mouse = { .x = 320, .y = 600 };
    CHECK(navigator.handle(drag, MapArea));
    // Let go.
    CHECK(!navigator.handle(mouse_at(320, 600), MapArea));
    CHECK(!navigator.dragging);
    // A press outside the map isn't a drag.
    auto outside = mouse_at(10, 10);
    outside.pressed = true;
    outside.down = true;
    navigator.handle(outside, MapArea);
    CHECK(!navigator.dragging);
}

// The middle of the map stays on the route's box.
void test_navigator_pan_limits()
{
    auto navigator = started();
    navigator.zoom_by(3);
    auto press = mouse_at(300, 200);
    press.pressed = true;
    press.down = true;
    navigator.handle(press, MapArea);
    auto drag = mouse_at(300 + 100'000, 200 - 100'000);
    drag.down = true;
    navigator.handle(drag, MapArea);
    auto const middle = world_coordinates(navigator.view.center, navigator.view.zoom);
    CHECK(near(middle.lon, route.west(), 1e-4));
    CHECK(near(middle.lat, route.south(), 1e-4));
}

void test_navigator_resize()
{
    auto navigator = started();
    CHECK(navigator.resize(300, 200));
    auto const refit = MapView::fit(route, 300, 200);
    CHECK(refit.has_value() && navigator.view.zoom == refit->zoom);
    // Moved: the same center and zoom at another size.
    navigator.zoom_by(1);
    auto const before = navigator.view;
    CHECK(navigator.resize(900, 600));
    CHECK_EQ(navigator.view.zoom, before.zoom);
    CHECK(navigator.view.center == before.center);
    CHECK_EQ(navigator.view.width, 900.0f);
    // Back to the whole route: refits on resize again.
    CHECK(navigator.whole_route());
    CHECK(!navigator.moved);
    CHECK(!navigator.whole_route());
}
}

int main()
{
    test_all_tiles_arrive();
    test_failed_tiles_are_counted_and_not_sent();
    test_nothing_is_fetched_when_cancelled();
    test_cancel_while_fetching();
    test_empty_download();
    test_cancel_while_fetching_last_tile();
    test_map_grid_covers_route();
    test_tile_index_matches_image_position();
    test_cached_tile();
    test_get_tile_prefers_cache();
    test_map_without_zoom_level();
    test_world_pixel();
    test_fit_covers_route();
    test_screen_and_world();
    test_zoom_keeps_point_under_mouse();
    test_pan_and_resize();
    test_tiles_cover_view();
    test_cover_with_ancestors();
    test_cover_with_children();
    test_navigator_wheel_zooms_around_mouse();
    test_navigator_zoom_limits();
    test_navigator_drag_pans();
    test_navigator_pan_limits();
    test_navigator_resize();
    return ST::Test::test_result();
}
