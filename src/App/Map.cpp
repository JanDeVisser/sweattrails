/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <numbers>
#include <system_error>

#include <curl/curl.h>

#include <Error.h>
#include <IO.h>
#include <Logging.h>
#include <Process.h>

#include <Coordinates.h>
#include <Map.h>
#include <storage/Storage.h>

namespace ST {

namespace fs = std::filesystem;

namespace {

// constexpr float32 PI = std::numbers::pi_v<float32>;

float32 degrees_to_radians(float32 degrees)
{
    return degrees * PI / 180.0f;
}

float32 radians_to_degrees(float32 radians)
{
    return radians * 180.0f / PI;
}

float32 tile_count(u8 zoom)
{
    return static_cast<float32>(1u << zoom);
}

}

Coordinates Tile::coordinates() const
{
    auto const n = tile_count(zoom);
    auto const lon_deg = static_cast<float32>(x) / n * 360.0f - 180.0f;
    auto const lat_rad = std::atan(std::sinh(PI * (1.0f - 2.0f * static_cast<float32>(y) / n)));
    return Coordinates { .lat = radians_to_degrees(lat_rad), .lon = lon_deg };
}

bool Tile::contains(Coordinates const &coordinates) const
{
    return coordinates.in_box(box());
}

Tile Tile::for_coordinates(Coordinates const &pos, u8 zoom)
{
    auto const lat_rad = degrees_to_radians(pos.lat);
    auto const n = tile_count(zoom);
    auto const xtile = static_cast<u32>((pos.lon + 180.0f) / 360.0f * n);
    auto const ytile = static_cast<u32>((1.0f - std::asinh(std::tan(lat_rad)) / PI) / 2.0f * n);
    return Tile { .x = xtile, .y = ytile, .zoom = zoom };
}

Box Tile::box() const
{
    auto const box_sw = Tile { .x = x, .y = y + 1, .zoom = zoom }.coordinates();
    auto const box_ne = Tile { .x = x + 1, .y = y, .zoom = zoom }.coordinates();
    return Box { .sw = box_sw, .ne = box_ne };
}

// TODO: This uses std::filesystem calls that throw, which end the app on
// e.g. an unreadable directory. Use the error_code overloads, as the job
// thread code in Storage does.
std::expected<std::string, LibCError> Tile::get_cached_tile(Storage const &storage) const
{
    auto const file_name = (storage.tiles_dir / std::format("{}/{}/{}.png", zoom, x, y)).string();
    if (!fs::exists(file_name)) {
        return std::unexpected(LibCError { ENOENT });
    }
    return read_file_by_name(file_name);
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *stream)
{
    try {
        ((std::ofstream *) stream)->write(ptr, size * nmemb);
        return size * nmemb;
    } catch (std::ios_base::failure const &e) {
        std::cerr << "Failure downloading using libcurl: " << e.what() << '\n';
        return 0;
    }
}

std::expected<void, std::variant<CURLcode, LibCError>> download(std::string url, fs::path dest)
{
    static CURL *curl = nullptr;

    if (curl == nullptr) {
        CURLcode result = curl_global_init(CURL_GLOBAL_ALL);
        if (result != CURLE_OK) {
            return std::unexpected(result);
        }

        curl = curl_easy_init();
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "sweattrails/1.0");
    // Fail on an HTTP error, such as the 403 or 429 the tile server sends to
    // clients that break its usage policy, instead of caching the error page
    // as the tile.
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

    /* open the file */
    auto file = std::ofstream(dest, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file) {
        return std::unexpected(LibCError { });
    }

    /* write the page body to this file handle */
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);

    /* Perform the request, result gets the return code */
    auto result = curl_easy_perform(curl);
    /* Check for errors */
    if (result != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(result) << "\n";
        return std::unexpected(result);
    }
    file.close();
    if (!file) {
        return std::unexpected(LibCError { "Could not write `{}`", dest.string() });
    }
    return { };
}

std::expected<std::string, LibCError> Tile::get_tile(Storage const &storage) const
{
    if (auto cached = get_cached_tile(storage); cached) {
        return cached;
    }
    auto const url = std::format("https://tile.openstreetmap.org/{}/{}/{}.png", zoom, x, y);

    auto const tile_dir = TRY_EVAL(ensure_directory(storage.tiles_dir / std::format("{}/{}", zoom, x)));
    auto const tile_file = tile_dir / std::format("{}.png", y);

    // get_cached_tile() takes any file with the tile's name for the tile. So
    // download to another name, and only rename it when it's complete: the
    // UI thread may read the cache during the download, and the app may quit
    // in the middle of one.
    auto const part_file = tile_dir / std::format("{}.png.part", y);

    auto const exit_code = download(url, part_file);
    if (!exit_code.has_value()) {
        std::error_code ec { };
        fs::remove(part_file, ec);
        return std::unexpected(
            std::visit(
                overloaded {
                    [&url](CURLcode c) -> LibCError {
                        return LibCError { std::format("Error fetching tile `{}`: curl error: {} ({})", url, curl_easy_strerror(c), static_cast<int>(c)) };
                    },
                    [](LibCError c) -> LibCError {
                        return c;
                    },
                },
                exit_code.error()));
    }
    std::error_code ec { };
    fs::rename(part_file, tile_file, ec);
    if (ec) {
        auto const message = ec.message();
        fs::remove(part_file, ec);
        return std::unexpected(LibCError { "Could not cache tile `{}`: {}", url, message });
    }
    trace(Map, "fetched tile {}", url);
    return get_cached_tile(storage);
}

std::string Tile::to_string() const
{
    return std::format("zoom {}/x: {} y: {}/box: `{}`", zoom, x, y, box());
}

size_t fetch_tiles(TileDownload const &download, TileFetcher const &fetch, TileReceiver const &on_tile)
{
    size_t failed = 0;
    for (auto const &[index, tile] : download.tiles) {
        if (download.cancelled) {
            break;
        }
        auto png = fetch(tile);
        if (download.cancelled) {
            break;
        }
        if (!png) {
            info(Map, "Could not get tile {}: {}", tile, png.error().to_string());
            ++failed;
            continue;
        }
        on_tile(index, std::move(png.value()));
    }
    return failed;
}

/**
 * Initializes a Map to display by calculating its zoom level and the grid
 * of OSM tiles that covers it. Getting the tiles is up to the caller.
 *
 * @param b Bounding box in real world map coordinates of the map to be
 * displayed.
 * @param width Width in pixels of the Map image area
 * @param height Height in pixels of the Map image area
 */
std::expected<Map, LibCError> Map::init(Box const &b, u16 width, u16 height)
{
    auto const mid = b.center();
    trace(Map, "middle of the box is `{}`", mid);
    for (u8 zoom = 16; zoom > 0; --zoom) {
        auto const mid_tile = Tile::for_coordinates(mid, zoom);
        auto const tile_box = mid_tile.box();
        if ((256 * (b.width() * 1.1f) / tile_box.width() > width) || (256 * (b.height() * 1.1f) / tile_box.height() > height)) {
            trace(Map, "Mid tile `{}` does not fit", mid_tile);
            continue;
        }

        trace(Map, "Mid tile `{}` DOES fit", mid_tile);

        auto const n = tile_count(zoom);
        auto const fx = (mid.lon + 180.0f) / 360.0f * n;
        auto const fy = (1.0f - std::asinh(std::tan(degrees_to_radians(mid.lat))) / PI) / 2.0f * n;
        auto const left = fx - width / 512.0f; // in tile units
        auto const top = fy - height / 512.0f;
        u32 const  x0 = static_cast<u32>(std::floor(left));
        u32 const  y0 = static_cast<u32>(std::floor(top));
        u16 const  columns = static_cast<u16>(std::ceil(fx + width / 512.0f) - x0);
        u16 const  rows = static_cast<u16>(std::ceil(fy + height / 512.0f) - y0);

        Rectangle rect {
            .x = (left - x0) * 256.0f,
            .y = (top - y0) * 256.0f,
            .width = static_cast<float>(width),
            .height = static_cast<float>(height),
        };
        trace(Map, "Rectangle {}x{}@{},{}", rect.width, rect.height, rect.x, rect.y);
        return Map {
            .zoom = zoom,
            .x = x0,
            .y = y0,
            .width = width,
            .height = height,
            .columns = columns,
            .rows = rows,
            .rectangle = rect,
            .num_tiles = static_cast<u16>(columns * rows),
        };
    }
    return std::unexpected(LibCError { "Could not find a zoom level for the requested box" });
}

Tile Map::tile(size_t ix) const
{
    // trace(Map, "tile({})", ix);
    assert(ix < num_tiles);
    return tile_xy(static_cast<u32>(ix % columns), static_cast<u32>(ix / columns));
}

Tile Map::tile_xy(u32 tile_x, u32 tile_y) const
{
    // trace(Map, "tile_xy({}, {})", tile_x, tile_y);
    assert(tile_x < columns && tile_y < rows);
    return Tile { .x = x + tile_x, .y = y + tile_y, .zoom = zoom };
}

Coordinates Map::coordinates(float32 map_x, float32 map_y) const
{
    // trace(Map, "coordinates({}, {})", map_x, map_y);
    assert(map_x >= 0 && map_x <= static_cast<float32>(columns * 256));
    assert(map_y >= 0 && map_y <= static_cast<float32>(rows * 256));
    auto const t = tile_xy(static_cast<u32>(std::trunc(map_x)), static_cast<u32>(std::trunc(map_y)));
    auto const b = t.box();
    return Coordinates {
        .lat = b.sw.lat + (1 - (map_y - std::trunc(map_y))) * (b.ne.lat - b.sw.lat),
        .lon = b.sw.lon + (map_x - std::trunc(map_x)) * (b.ne.lon - b.sw.lon),
    };
}

double world_size(u8 zoom)
{
    return std::ldexp(256.0, zoom);
}

WorldPoint world_pixel(Coordinates const &coordinates, u8 zoom)
{
    auto const size = world_size(zoom);
    auto const lat = static_cast<double>(coordinates.lat) * std::numbers::pi / 180.0;
    return WorldPoint {
        .x = (static_cast<double>(coordinates.lon) + 180.0) / 360.0 * size,
        .y = (1.0 - std::asinh(std::tan(lat)) / std::numbers::pi) / 2.0 * size,
    };
}

Coordinates world_coordinates(WorldPoint const &point, u8 zoom)
{
    auto const size = world_size(zoom);
    auto const lat = std::atan(std::sinh(std::numbers::pi * (1.0 - 2.0 * point.y / size)));
    return Coordinates {
        .lat = static_cast<float32>(lat * 180.0 / std::numbers::pi),
        .lon = static_cast<float32>(point.x / size * 360.0 - 180.0),
    };
}

std::optional<MapView> MapView::fit(Box const &route, float width, float height)
{
    for (u8 zoom = MaxFitZoom; zoom > 0; --zoom) {
        auto const nw = world_pixel(Coordinates { .lat = route.north(), .lon = route.west() }, zoom);
        auto const se = world_pixel(Coordinates { .lat = route.south(), .lon = route.east() }, zoom);
        if ((se.x - nw.x) * 1.1 > width || (se.y - nw.y) * 1.1 > height) {
            continue;
        }
        return MapView {
            .zoom = zoom,
            .center = WorldPoint { .x = (nw.x + se.x) / 2, .y = (nw.y + se.y) / 2 },
            .width = width,
            .height = height,
        };
    }
    return { };
}

Vector2 MapView::to_screen(WorldPoint const &point) const
{
    return Vector2 {
        .x = static_cast<float>(point.x - center.x + width / 2.0),
        .y = static_cast<float>(point.y - center.y + height / 2.0),
    };
}

WorldPoint MapView::to_world(Vector2 const &screen) const
{
    return WorldPoint {
        .x = center.x + screen.x - width / 2.0,
        .y = center.y + screen.y - height / 2.0,
    };
}

MapView MapView::zoomed(int steps, Vector2 const &around) const
{
    auto const new_zoom = static_cast<u8>(std::clamp(static_cast<int>(zoom) + steps, 0, 31));
    auto const scale = std::ldexp(1.0, static_cast<int>(new_zoom) - static_cast<int>(zoom));
    auto const fixed = to_world(around);
    // The world point under `around` scales with the world, and stays under
    // `around`: the center moves with it.
    auto ret = *this;
    ret.zoom = new_zoom;
    ret.center = WorldPoint {
        .x = fixed.x * scale - (around.x - width / 2.0),
        .y = fixed.y * scale - (around.y - height / 2.0),
    };
    return ret;
}

MapView MapView::panned(Vector2 const &delta) const
{
    auto ret = *this;
    ret.center = WorldPoint { .x = center.x - delta.x, .y = center.y - delta.y };
    return ret;
}

MapView MapView::resized(float new_width, float new_height) const
{
    auto ret = *this;
    ret.width = new_width;
    ret.height = new_height;
    return ret;
}

std::vector<PlacedTile> MapView::tiles() const
{
    std::vector<PlacedTile> ret;
    auto const              left = center.x - width / 2.0;
    auto const              top = center.y - height / 2.0;
    auto const              count = static_cast<i64>(1) << zoom;
    auto const              first_x = std::max(static_cast<i64>(std::floor(left / 256.0)), i64 { 0 });
    auto const              first_y = std::max(static_cast<i64>(std::floor(top / 256.0)), i64 { 0 });
    auto const              end_x = std::min(static_cast<i64>(std::ceil((left + width) / 256.0)), count);
    auto const              end_y = std::min(static_cast<i64>(std::ceil((top + height) / 256.0)), count);
    for (auto y = first_y; y < end_y; ++y) {
        for (auto x = first_x; x < end_x; ++x) {
            ret.push_back(PlacedTile {
                .tile = Tile { .x = static_cast<u32>(x), .y = static_cast<u32>(y), .zoom = zoom },
                .screen = Rectangle {
                    .x = static_cast<float>(static_cast<double>(x) * 256.0 - left),
                    .y = static_cast<float>(static_cast<double>(y) * 256.0 - top),
                    .width = 256,
                    .height = 256,
                },
            });
        }
    }
    return ret;
}

std::vector<TilePiece> cover(Tile const &tile, TileLoaded const &loaded)
{
    for (u8 up = 1; up <= 3 && up <= tile.zoom; ++up) {
        auto const ancestor = Tile { .x = tile.x >> up, .y = tile.y >> up, .zoom = static_cast<u8>(tile.zoom - up) };
        if (!loaded(ancestor)) {
            continue;
        }
        auto const size = 256.0f / static_cast<float>(1u << up);
        auto const mask = (1u << up) - 1;
        return { TilePiece {
            .source = ancestor,
            .source_area = Rectangle {
                .x = static_cast<float>(tile.x & mask) * size,
                .y = static_cast<float>(tile.y & mask) * size,
                .width = size,
                .height = size,
            },
            .destination = Rectangle { .x = 0, .y = 0, .width = 256, .height = 256 },
        } };
    }
    std::vector<TilePiece> ret;
    for (u32 dy = 0; dy < 2; ++dy) {
        for (u32 dx = 0; dx < 2; ++dx) {
            auto const child = Tile { .x = tile.x * 2 + dx, .y = tile.y * 2 + dy, .zoom = static_cast<u8>(tile.zoom + 1) };
            if (loaded(child)) {
                ret.push_back(TilePiece {
                    .source = child,
                    .source_area = Rectangle { .x = 0, .y = 0, .width = 256, .height = 256 },
                    .destination = Rectangle {
                        .x = static_cast<float>(dx) * 128.0f,
                        .y = static_cast<float>(dy) * 128.0f,
                        .width = 128,
                        .height = 128,
                    },
                });
            }
        }
    }
    return ret;
}

bool MapNavigator::start(Box const &new_route, float width, float height)
{
    route = new_route;
    moved = false;
    dragging = false;
    wheel = { };
    auto const fitted = MapView::fit(route, width, height);
    if (!fitted) {
        return false;
    }
    view = *fitted;
    fit_zoom = view.zoom;
    return true;
}

bool MapNavigator::handle(ScrollInput const &input, Rectangle const &viewport)
{
    if (dragging) {
        if (!input.down) {
            dragging = false;
            return false;
        }
        auto const delta = Vector2 { .x = input.mouse.x - drag_from.x, .y = input.mouse.y - drag_from.y };
        drag_from = input.mouse;
        if (delta.x == 0 && delta.y == 0) {
            return false;
        }
        auto const before = view.center;
        view = view.panned(delta);
        clamp_center();
        moved = true;
        return view.center != before;
    }
    if (!CheckCollisionPointRec(input.mouse, viewport)) {
        return false;
    }
    if (input.pressed) {
        dragging = true;
        drag_from = input.mouse;
        return false;
    }
    if (input.wheel != 0) {
        auto const notches = wheel.add(input.wheel);
        if (notches != 0) {
            return zoom_by(notches, Vector2 { .x = input.mouse.x - viewport.x, .y = input.mouse.y - viewport.y });
        }
    }
    return false;
}

bool MapNavigator::zoom_by(int steps, std::optional<Vector2> around)
{
    auto const target = std::clamp(static_cast<int>(view.zoom) + steps, static_cast<int>(min_zoom()), static_cast<int>(max_zoom()));
    if (target == view.zoom) {
        return false;
    }
    view = view.zoomed(target - view.zoom, around.value_or(Vector2 { .x = view.width / 2, .y = view.height / 2 }));
    clamp_center();
    moved = true;
    return true;
}

bool MapNavigator::whole_route()
{
    auto const before = view;
    if (!start(route, view.width, view.height)) {
        return false;
    }
    return view.zoom != before.zoom || view.center != before.center;
}

bool MapNavigator::resize(float width, float height)
{
    if (width == view.width && height == view.height) {
        return false;
    }
    if (!moved) {
        if (auto const fitted = MapView::fit(route, width, height); fitted) {
            view = *fitted;
            fit_zoom = view.zoom;
            return true;
        }
    }
    view = view.resized(width, height);
    return true;
}

u8 MapNavigator::min_zoom() const
{
    return (fit_zoom > ZoomOutPastFit) ? static_cast<u8>(fit_zoom - ZoomOutPastFit) : u8 { 0 };
}

u8 MapNavigator::max_zoom() const
{
    return std::max(MaxMapZoom, fit_zoom);
}

// The middle of the map stays on the route's box: the user can look
// around the route, but not lose it.
void MapNavigator::clamp_center()
{
    auto const nw = world_pixel(Coordinates { .lat = route.north(), .lon = route.west() }, view.zoom);
    auto const se = world_pixel(Coordinates { .lat = route.south(), .lon = route.east() }, view.zoom);
    view.center.x = std::clamp(view.center.x, nw.x, se.x);
    view.center.y = std::clamp(view.center.y, nw.y, se.y);
}

}
