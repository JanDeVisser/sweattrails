/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the map code that needs neither a window nor the network: the view
 * (MapView), its zooming and panning (MapNavigator), the placeholders for
 * tiles that aren't loaded (cover()), and fetch_tiles(), the loop the job
 * thread runs to download the tiles a view is missing. A fake fetcher
 * stands in for the download.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <expected>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <vector>

#include <Error.h>

#include <Coordinates.h>
#include <Map.h>
#include <storage/Storage.h>

#include <TestApp.h>

using namespace ST;

namespace {

// A download of `count` tiles, the i'th at x = i.
pTileDownload make_download(size_t count)
{
    auto download = std::make_shared<TileDownload>();
    for (size_t ix = 0; ix < count; ++ix) {
        download->tiles.push_back(Tile { .x = static_cast<u32>(ix), .y = 0, .zoom = 10 });
    }
    return download;
}

std::string png_for(Tile const &tile)
{
    return std::format("png {}", tile.x);
}

struct Received {
    size_t      index; // The tile's x
    std::string png;
};

struct Fetch {
    std::vector<u32>      fetched { };
    std::vector<Received> received { };

    std::vector<Tile> run(TileDownload const &download, TileFetcher const &fetch)
    {
        return fetch_tiles(
            download,
            [this, &fetch](Tile const &tile) {
                fetched.push_back(tile.x);
                return fetch(tile);
            },
            [this](Tile const &tile, std::string png) {
                received.push_back(Received { tile.x, std::move(png) });
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
    CHECK_EQ(failed.size(), 0uz);
    CHECK_EQ(f.received.size(), 3uz);
    for (size_t ix = 0; ix < f.received.size(); ++ix) {
        CHECK_EQ(f.received[ix].index, ix);
        CHECK_EQ(f.received[ix].png, png_for(download->tiles[ix]));
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
    CHECK_EQ(failed.size(), 2uz);
    CHECK(failed.size() == 2 && failed[0].x == 1 && failed[1].x == 2);
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
    CHECK_EQ(failed.size(), 0uz);
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
    CHECK_EQ(failed.size(), 0uz);
    CHECK_EQ(f.fetched.size(), 2uz);
    CHECK_EQ(f.received.size(), 1uz);
    if (!f.received.empty()) {
        CHECK_EQ(f.received[0].index, 0uz);
    }
}

// A tile that failed before the download was cancelled is still a
// failure: the map doesn't ask for it again.
void test_failure_before_cancel_is_returned()
{
    auto  download = make_download(4);
    Fetch f;
    auto  failed = f.run(*download, [&download](Tile const &tile) -> std::expected<std::string, LibCError> {
        if (tile.x == 0) {
            return std::unexpected(LibCError { "429 Too Many Requests" });
        }
        download->cancelled = true;
        return png_for(tile);
    });
    CHECK(failed.size() == 1 && failed[0].x == 0);
    CHECK_EQ(f.fetched.size(), 2uz);
}

void test_empty_download()
{
    auto  download = make_download(0);
    Fetch f;
    auto  failed = f.run(*download, fetch_ok);
    CHECK_EQ(failed.size(), 0uz);
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
    CHECK_EQ(failed.size(), 0uz);
    CHECK_EQ(f.fetched.size(), 3uz);
    CHECK_EQ(f.received.size(), 2uz);
}

// A ride around Toronto's waterfront, on a map of 600x400 pixels.
Box const route { .sw = { .lat = 43.62f, .lon = -79.45f }, .ne = { .lat = 43.67f, .lon = -79.35f } };

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

// The whole route, centered, at the highest zoom where it fits.
void test_fit_covers_route()
{
    for (auto const [width, height] : { std::pair<u16, u16> { 600, 400 }, { 300, 700 }, { 1000, 250 } }) {
        auto const view = MapView::fit(route, width, height);
        CHECK(view.has_value());
        if (!view) {
            continue;
        }
        CHECK(view->zoom > 0 && view->zoom <= MaxFitZoom);
        auto const extent = [](u8 zoom) {
            auto const nw = world_pixel(Coordinates { .lat = route.north(), .lon = route.west() }, zoom);
            auto const se = world_pixel(Coordinates { .lat = route.south(), .lon = route.east() }, zoom);
            return std::pair { se.x - nw.x, se.y - nw.y };
        };
        auto const [w, h] = extent(view->zoom + 1);
        CHECK(view->zoom == MaxFitZoom || w * 1.1 > width || h * 1.1 > height);
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

void test_missing_tiles()
{
    MapView const view { .zoom = 10, .center = { .x = 1000, .y = 2000 }, .width = 600, .height = 400 };
    auto const    missing = missing_tiles(view, [](Tile const &t) { return t.x != 3; });
    CHECK_EQ(missing.size(), 2uz); // x 3, y 7 and 8
    for (auto const &tile : missing) {
        CHECK_EQ(tile.x, 3u);
    }
    CHECK(missing_tiles(view, [](Tile const &) { return true; }).empty());
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

// The zoom Map::init() picked before MapView: the highest up to 16 where the
// route, with 10% to spare, fits, measured with the middle tile's box.
u8 zoom_before_map_view(Box const &b, u16 width, u16 height)
{
    auto const mid = b.center();
    for (u8 zoom = 16; zoom > 0; --zoom) {
        auto const tile_box = Tile::for_coordinates(mid, zoom).box();
        if ((256 * (b.width() * 1.1f) / tile_box.width() > width) || (256 * (b.height() * 1.1f) / tile_box.height() > height)) {
            continue;
        }
        return zoom;
    }
    return 0;
}

// fit() picks the zoom the map had before zooming, for routes of all sizes
// and latitudes, and viewports of all shapes.
void test_fit_zoom_as_before()
{
    std::vector<Box> const routes {
        route,
        Box { .sw = { .lat = 43.0f, .lon = -80.5f }, .ne = { .lat = 44.2f, .lon = -78.9f } },   // A long ride
        Box { .sw = { .lat = 69.60f, .lon = 18.85f }, .ne = { .lat = 69.70f, .lon = 19.05f } }, // Tromsø
        Box { .sw = { .lat = 40.0f, .lon = 2.0f }, .ne = { .lat = 48.0f, .lon = 3.0f } },       // North to south
        Box { .sw = { .lat = -33.95f, .lon = 18.40f }, .ne = { .lat = -33.90f, .lon = 18.45f } },
        Box { .sw = { .lat = 52.37f, .lon = 4.89f }, .ne = { .lat = 52.37f, .lon = 4.89f } }, // One point
    };
    std::vector<std::pair<u16, u16>> const sizes { { 600, 400 }, { 300, 700 }, { 1000, 250 }, { 431, 517 }, { 1280, 900 } };
    for (auto const &r : routes) {
        for (auto const [width, height] : sizes) {
            auto const before = zoom_before_map_view(r, width, height);
            auto const view = MapView::fit(r, width, height);
            CHECK(view.has_value());
            if (!view) {
                continue;
            }
            if (view->zoom != before) {
                std::println(stderr, "fit({}, {}x{}): zoom {}, before {}", r, width, height, view->zoom, before);
            }
            CHECK_EQ(view->zoom, before);
        }
    }
}

// Every pixel of the view that's in the world is on exactly one of the
// view's tiles, and every tile is in view; pixels outside the world are on
// none.
void check_tiles_cover_exactly(MapView const &view)
{
    auto const tiles = view.tiles();
    auto const size = world_size(view.zoom);
    for (auto const &placed : tiles) {
        auto const &s = placed.screen;
        CHECK(s.x < view.width && s.y < view.height && s.x + s.width > 0 && s.y + s.height > 0);
        CHECK_EQ(placed.tile.zoom, view.zoom);
        // Where the view puts the tile is where it is in the world.
        auto const world = view.to_world(Vector2 { .x = s.x, .y = s.y });
        CHECK(near(world.x, placed.tile.x * 256.0, 1e-2) && near(world.y, placed.tile.y * 256.0, 1e-2));
    }
    for (float y = 0.5f; y < view.height; y += 37.0f) {
        for (float x = 0.5f; x < view.width; x += 37.0f) {
            auto const world = view.to_world(Vector2 { .x = x, .y = y });
            auto const in_world = world.x >= 0 && world.y >= 0 && world.x < size && world.y < size;
            auto const on = std::ranges::count_if(tiles, [x, y](PlacedTile const &placed) {
                return CheckCollisionPointRec(Vector2 { .x = x, .y = y }, placed.screen);
            });
            CHECK_EQ(static_cast<int>(on), in_world ? 1 : 0);
        }
    }
}

void test_tiles_cover_view_exactly()
{
    check_tiles_cover_exactly(MapView { .zoom = 10, .center = { .x = 1000, .y = 2000 }, .width = 600, .height = 400 });
    // On tile boundaries.
    check_tiles_cover_exactly(MapView { .zoom = 10, .center = { .x = 1024 + 300, .y = 2048 + 200 }, .width = 600, .height = 400 });
    // The world's bottom right corner.
    check_tiles_cover_exactly(MapView { .zoom = 3, .center = { .x = world_size(3), .y = world_size(3) }, .width = 600, .height = 400 });
    // The whole world at zoom 0 and 1 in a larger viewport.
    MapView const zoom0 { .zoom = 0, .center = { .x = 128, .y = 128 }, .width = 900, .height = 700 };
    check_tiles_cover_exactly(zoom0);
    CHECK_EQ(zoom0.tiles().size(), 1uz);
    MapView const zoom1 { .zoom = 1, .center = { .x = 256, .y = 256 }, .width = 900, .height = 700 };
    check_tiles_cover_exactly(zoom1);
    CHECK_EQ(zoom1.tiles().size(), 4uz);
    // Zoom 18, far from the origin, to the pixel.
    auto const center = world_pixel(Coordinates { .lat = 43.65f, .lon = -79.38f }, 18);
    check_tiles_cover_exactly(MapView { .zoom = 18, .center = center, .width = 777, .height = 555 });
    // Zoom 18 at the world's last tile.
    check_tiles_cover_exactly(MapView { .zoom = 18, .center = { .x = world_size(18) - 10, .y = world_size(18) - 10 }, .width = 600, .height = 400 });
}

// zoomed() keeps the point under the mouse to the pixel, also at zoom 18
// far from the origin.
void test_zoom_at_street_level()
{
    auto const    center = world_pixel(Coordinates { .lat = 43.65f, .lon = -79.38f }, 17);
    MapView const view { .zoom = 17, .center = center, .width = 600, .height = 400 };
    Vector2 const mouse { .x = 17, .y = 391 };
    auto const    in = view.zoomed(1, mouse);
    CHECK_EQ(in.zoom, u8 { 18 });
    auto const before = view.to_world(mouse);
    auto const after = in.to_world(mouse);
    CHECK(near(after.x, before.x * 2, 1e-3) && near(after.y, before.y * 2, 1e-3));
    auto const back = in.zoomed(-1, mouse);
    CHECK(near(back.center.x, view.center.x, 1e-3) && near(back.center.y, view.center.y, 1e-3));
}

void test_cover_great_grandparent_and_edges()
{
    // Three levels up: an eighth of the great-grandparent, 32 pixels.
    Tile const tile { .x = 13, .y = 6, .zoom = 10 };
    auto const great = Tile { .x = 1, .y = 0, .zoom = 7 };
    auto const pieces = cover(tile, [&](Tile const &t) { return t == great; });
    CHECK_EQ(pieces.size(), 1uz);
    if (!pieces.empty()) {
        CHECK_EQ(pieces[0].source, great);
        // 13 & 7 = 5, 6 & 7 = 6.
        CHECK_EQ(pieces[0].source_area.x, 160.0f);
        CHECK_EQ(pieces[0].source_area.y, 192.0f);
        CHECK_EQ(pieces[0].source_area.width, 32.0f);
        CHECK_EQ(pieces[0].source_area.height, 32.0f);
        CHECK_EQ(pieces[0].destination.x, 0.0f);
        CHECK_EQ(pieces[0].destination.height, 256.0f);
    }
    // The world's tile has no ancestors: only its children.
    Tile const world { .x = 0, .y = 0, .zoom = 0 };
    CHECK(cover(world, [](Tile const &t) { return t.zoom == 0; }).empty());
    CHECK_EQ(cover(world, [](Tile const &t) { return t.zoom == 1; }).size(), 4uz);
    // At zoom 2 the world's tile is two levels up, and there is no third.
    Tile const low { .x = 3, .y = 1, .zoom = 2 };
    auto const from_world = cover(low, [](Tile const &t) { return t.zoom == 0; });
    CHECK_EQ(from_world.size(), 1uz);
    if (!from_world.empty()) {
        CHECK_EQ(from_world[0].source_area.x, 192.0f);
        CHECK_EQ(from_world[0].source_area.y, 64.0f);
        CHECK_EQ(from_world[0].source_area.width, 64.0f);
    }
    // Street level, the last tile of the world, from its parent.
    Tile const street { .x = (1u << 18) - 1, .y = (1u << 18) - 2, .zoom = 18 };
    auto const parent = Tile { .x = street.x / 2, .y = street.y / 2, .zoom = 17 };
    auto const from_parent = cover(street, [&](Tile const &t) { return t == parent; });
    CHECK(from_parent.size() == 1 && from_parent[0].source_area.x == 128.0f && from_parent[0].source_area.y == 0.0f);
}

// Zoomed out: each of the four children goes in its own quarter.
void test_cover_with_all_children()
{
    Tile const tile { .x = 5, .y = 2, .zoom = 4 };
    auto const pieces = cover(tile, [](Tile const &t) { return t.zoom == 5; });
    CHECK_EQ(pieces.size(), 4uz);
    for (auto const &piece : pieces) {
        CHECK_EQ(piece.source_area.width, 256.0f);
        CHECK_EQ(piece.destination.width, 128.0f);
        CHECK_EQ(piece.destination.height, 128.0f);
        CHECK_EQ(piece.destination.x, (piece.source.x - 10) * 128.0f);
        CHECK_EQ(piece.destination.y, (piece.source.y - 4) * 128.0f);
    }
    // An ancestor goes before the children.
    auto const ancestor_first = cover(tile, [](Tile const &t) { return t.zoom == 5 || t.zoom == 3; });
    CHECK(ancestor_first.size() == 1 && ancestor_first[0].source.zoom == 3);
}

// A click on the map, without moving the mouse, doesn't move it: a resize
// still fits the whole route.
void test_navigator_click_does_nothing()
{
    auto       navigator = started();
    auto const before = navigator.view;
    auto       press = mouse_at(300, 200);
    press.pressed = true;
    press.down = true;
    CHECK(!navigator.handle(press, MapArea));
    auto hold = mouse_at(300, 200);
    hold.down = true;
    CHECK(!navigator.handle(hold, MapArea));
    CHECK(!navigator.handle(mouse_at(300, 200), MapArea));
    CHECK(!navigator.dragging);
    CHECK(!navigator.moved);
    CHECK(navigator.view.center == before.center);
    CHECK(navigator.resize(300, 200));
    CHECK_EQ(navigator.view.zoom, MapView::fit(route, 300, 200)->zoom);
}

// The keys zoom around the middle, which stays where it is.
void test_navigator_keys_zoom_around_middle()
{
    auto       navigator = started();
    auto const middle = world_coordinates(navigator.view.center, navigator.view.zoom);
    CHECK(navigator.zoom_by(1));
    auto const in = world_coordinates(navigator.view.center, navigator.view.zoom);
    CHECK(near(in.lat, middle.lat, 1e-5) && near(in.lon, middle.lon, 1e-5));
    CHECK(navigator.zoom_by(-2));
    auto const out = world_coordinates(navigator.view.center, navigator.view.zoom);
    CHECK(near(out.lat, middle.lat, 1e-5) && near(out.lon, middle.lon, 1e-5));
    CHECK(navigator.moved);
}

// The center stays on the route's box on its north and east sides too, and
// when zooming out around a corner.
void test_navigator_pan_limits_north_east()
{
    auto navigator = started();
    navigator.zoom_by(3);
    auto press = mouse_at(300, 200);
    press.pressed = true;
    press.down = true;
    navigator.handle(press, MapArea);
    auto drag = mouse_at(300 - 100'000, 200 + 100'000);
    drag.down = true;
    navigator.handle(drag, MapArea);
    auto const middle = world_coordinates(navigator.view.center, navigator.view.zoom);
    CHECK(near(middle.lon, route.east(), 1e-4));
    CHECK(near(middle.lat, route.north(), 1e-4));
    navigator.handle(mouse_at(0, 0), MapArea);

    // The wheel at the viewport's corner, zoomed in: the point under the
    // mouse is off the route's box, and the center stays on it.
    auto wheel = mouse_at(MapArea.x + 1, MapArea.y + 1);
    wheel.wheel = -1;
    navigator.handle(wheel, MapArea);
    auto const nw = world_pixel(Coordinates { .lat = route.north(), .lon = route.west() }, navigator.view.zoom);
    auto const se = world_pixel(Coordinates { .lat = route.south(), .lon = route.east() }, navigator.view.zoom);
    CHECK(navigator.view.center.x >= nw.x - 1e-6 && navigator.view.center.x <= se.x + 1e-6);
    CHECK(navigator.view.center.y >= nw.y - 1e-6 && navigator.view.center.y <= se.y + 1e-6);
}

// A route that only fits at zoom 1 zooms out to 0, not past it.
void test_navigator_zoom_limits_low()
{
    Box const    continent { .sw = { .lat = 30.0f, .lon = -120.0f }, .ne = { .lat = 50.0f, .lon = -60.0f } };
    MapNavigator navigator;
    CHECK(navigator.start(continent, 300, 300));
    CHECK(navigator.view.zoom <= ZoomOutPastFit);
    CHECK_EQ(navigator.min_zoom(), u8 { 0 });
    navigator.zoom_by(-5);
    CHECK_EQ(navigator.view.zoom, u8 { 0 });
    CHECK_EQ(navigator.view.tiles().size(), 1uz);
}

// Back to the whole route after a drag: not moved any more, and a resize
// fits the route again.
void test_navigator_whole_route_after_drag()
{
    auto navigator = started();
    navigator.zoom_by(2);
    auto press = mouse_at(300, 200);
    press.pressed = true;
    press.down = true;
    navigator.handle(press, MapArea);
    auto drag = mouse_at(350, 260);
    drag.down = true;
    CHECK(navigator.handle(drag, MapArea));
    navigator.handle(mouse_at(350, 260), MapArea);
    CHECK(navigator.moved);
    CHECK(navigator.whole_route());
    CHECK(!navigator.moved);
    auto const fit = MapView::fit(route, MapArea.width, MapArea.height);
    CHECK(fit && navigator.view.zoom == fit->zoom && navigator.view.center == fit->center);
    CHECK(navigator.resize(300, 200));
    CHECK_EQ(navigator.view.zoom, MapView::fit(route, 300, 200)->zoom);
    // The zoom limits follow the new whole-route zoom.
    CHECK_EQ(navigator.min_zoom(), static_cast<u8>(navigator.view.zoom - ZoomOutPastFit));
}

// Not moved, and resized to where the route doesn't fit at any zoom: the
// view keeps its center and zoom rather than going.
void test_navigator_resize_too_small()
{
    Box const    continent { .sw = { .lat = 30.0f, .lon = -120.0f }, .ne = { .lat = 50.0f, .lon = -60.0f } };
    MapNavigator navigator;
    CHECK(navigator.start(continent, 600, 600));
    auto const before = navigator.view;
    CHECK(navigator.resize(20, 20));
    CHECK_EQ(navigator.view.zoom, before.zoom);
    CHECK(navigator.view.center == before.center);
    CHECK_EQ(navigator.view.width, 20.0f);
}

Tile tile_at(u32 x)
{
    return Tile { .x = x, .y = 0, .zoom = 10 };
}

// The least recently drawn go first, but not the ones in use, such as the
// whole route's tiles after a while zoomed in elsewhere.
void test_tiles_to_unload()
{
    std::vector<std::pair<Tile, size_t>> loaded { { tile_at(0), 5 }, { tile_at(1), 1 }, { tile_at(2), 3 }, { tile_at(3), 2 } };
    auto                                 unload = tiles_to_unload(loaded, { }, 2);
    CHECK(unload == (std::vector<Tile> { tile_at(1), tile_at(3) }));
    unload = tiles_to_unload(loaded, { tile_at(1) }, 2);
    CHECK(unload == (std::vector<Tile> { tile_at(3), tile_at(2) }));
    // All in use: they all stay.
    CHECK(tiles_to_unload(loaded, { tile_at(0), tile_at(1), tile_at(2), tile_at(3) }, 2).empty());
    CHECK(tiles_to_unload(loaded, { }, 4).empty());
}

// The view of test_missing_tiles(), without tile x 3: y 7 and 8.
void test_next_download()
{
    MapView const view { .zoom = 10, .center = { .x = 1000, .y = 2000 }, .width = 600, .height = 400 };
    auto const    have = [](Tile const &t) { return t.x != 3; };
    Tile const    t7 { .x = 3, .y = 7, .zoom = 10 };
    Tile const    t8 { .x = 3, .y = 8, .zoom = 10 };
    // None going on: a new one.
    auto next = next_download(view, have, nullptr);
    CHECK(next.has_value() && next->size() == 2);
    // One going on with these tiles, or more, as after one of its tiles
    // arrived: it goes on.
    std::vector<Tile> const current { t7, t8, Tile { .x = 9, .y = 9, .zoom = 10 } };
    CHECK(!next_download(view, have, &current).has_value());
    std::vector<Tile> const after_arrival { t7, t8 };
    CHECK(!next_download(view, [&](Tile const &t) { return t != t8; }, &after_arrival).has_value());
    // One going on without them, as after zooming: replaced.
    std::vector<Tile> const other { Tile { .x = 6, .y = 14, .zoom = 11 } };
    next = next_download(view, have, &other);
    CHECK(next.has_value() && next->size() == 2);
    // Nothing missing: one going on is cancelled, and none started.
    auto const all = [](Tile const &) { return true; };
    next = next_download(view, all, &other);
    CHECK(next.has_value() && next->empty());
    next = next_download(view, all, nullptr);
    CHECK(next.has_value() && next->empty());
}
}

int main()
{
    test_all_tiles_arrive();
    test_failed_tiles_are_counted_and_not_sent();
    test_nothing_is_fetched_when_cancelled();
    test_cancel_while_fetching();
    test_empty_download();
    test_failure_before_cancel_is_returned();
    test_cancel_while_fetching_last_tile();
    test_cached_tile();
    test_get_tile_prefers_cache();
    test_world_pixel();
    test_fit_covers_route();
    test_screen_and_world();
    test_zoom_keeps_point_under_mouse();
    test_pan_and_resize();
    test_tiles_cover_view();
    test_missing_tiles();
    test_tiles_to_unload();
    test_next_download();
    test_cover_with_ancestors();
    test_cover_with_children();
    test_navigator_wheel_zooms_around_mouse();
    test_navigator_zoom_limits();
    test_navigator_drag_pans();
    test_navigator_pan_limits();
    test_navigator_resize();
    test_fit_zoom_as_before();
    test_tiles_cover_view_exactly();
    test_zoom_at_street_level();
    test_cover_great_grandparent_and_edges();
    test_cover_with_all_children();
    test_navigator_click_does_nothing();
    test_navigator_keys_zoom_around_middle();
    test_navigator_pan_limits_north_east();
    test_navigator_zoom_limits_low();
    test_navigator_whole_route_after_drag();
    test_navigator_resize_too_small();
    return ST::Test::test_result();
}
