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
#include <Http.h>
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
std::expected<std::string, LibCError> Tile::get_cached_tile(fs::path const &cache) const
{
    auto const file_name = (cache / std::format("{}/{}/{}.png", zoom, x, y)).string();
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

std::expected<void, std::variant<CURLcode, LibCError>> download(std::string url, fs::path dest, DownloadLimit limit)
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
    // A connection that hangs would keep an overlay render waiting. The
    // handle is kept between downloads, so each sets all of these.
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    if (limit == DownloadLimit::Total) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 0L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 0L);
    } else {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    }
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, UserAgent);
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

// A file there is taken for complete, by get_cached_tile() among others.
// So it's downloaded to another name, and only renamed when it's complete:
// the UI thread may read the cache during the download, and the app may
// quit in the middle of one.
std::expected<void, LibCError> download_file(std::string const &url, fs::path const &file, DownloadLimit limit)
{
    TRY(ensure_directory(file.parent_path()));
    auto part_file = file;
    part_file += ".part";

    auto const exit_code = download(url, part_file, limit);
    if (!exit_code.has_value()) {
        std::error_code ec { };
        fs::remove(part_file, ec);
        return std::unexpected(
            std::visit(
                overloaded {
                    [&url](CURLcode c) -> LibCError {
                        return LibCError { std::format("Error fetching `{}`: curl error: {} ({})", url, curl_easy_strerror(c), static_cast<int>(c)) };
                    },
                    [](LibCError c) -> LibCError {
                        return c;
                    },
                },
                exit_code.error()));
    }
    std::error_code ec { };
    fs::rename(part_file, file, ec);
    if (ec) {
        auto const message = ec.message();
        fs::remove(part_file, ec);
        return std::unexpected(LibCError { "Could not save `{}`: {}", url, message });
    }
    return { };
}

std::expected<std::string, LibCError> Tile::get_tile(Storage const &storage) const
{
    if (auto cached = get_cached_tile(storage.tiles_dir); cached) {
        return cached;
    }
    auto const url = std::format("https://tile.openstreetmap.org/{}/{}/{}.png", zoom, x, y);
    TRY(download_file(url, storage.tiles_dir / std::format("{}/{}/{}.png", zoom, x, y)));
    trace(Map, "fetched tile {}", url);
    return get_cached_tile(storage.tiles_dir);
}

Tile Tile::ancestor(u8 up_to) const
{
    if (up_to >= zoom) {
        return *this;
    }
    auto const levels = zoom - up_to;
    return Tile { .x = x >> levels, .y = y >> levels, .zoom = up_to };
}

std::string Tile::to_string() const
{
    return std::format("zoom {}/x: {} y: {}/box: `{}`", zoom, x, y, box());
}

std::vector<Tile> fetch_tiles(TileDownload const &download, TileFetcher const &fetch, TileReceiver const &on_tile)
{
    std::vector<Tile> failed;
    for (auto const &tile : download.tiles) {
        if (download.cancelled) {
            break;
        }
        auto png = fetch(tile);
        if (download.cancelled) {
            break;
        }
        if (!png) {
            info(Map, "Could not get tile {}: {}", tile, png.error().to_string());
            failed.push_back(tile);
            continue;
        }
        on_tile(tile, std::move(png.value()));
    }
    return failed;
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

std::vector<Tile> tiles_to_unload(std::vector<std::pair<Tile, size_t>> loaded, std::set<Tile> const &keep, size_t max)
{
    std::ranges::stable_sort(loaded, { }, &std::pair<Tile, size_t>::second);
    std::vector<Tile> ret;
    auto              count = loaded.size();
    for (auto const &[tile, drawn] : loaded) {
        if (count <= max) {
            break;
        }
        if (!keep.contains(tile)) {
            ret.push_back(tile);
            --count;
        }
    }
    return ret;
}

std::vector<Tile> missing_tiles(MapView const &view, TileLoaded const &have)
{
    std::vector<Tile> ret;
    for (auto const &placed : view.tiles()) {
        if (!have(placed.tile)) {
            ret.push_back(placed.tile);
        }
    }
    return ret;
}

std::optional<std::vector<Tile>> next_download(MapView const &view, TileLoaded const &have, std::vector<Tile> const *current)
{
    std::vector<Tile> wanted;
    for (auto const &placed : view.tiles()) {
        wanted.push_back(placed.tile);
    }
    return next_download(wanted, have, current);
}

std::optional<std::vector<Tile>> next_download(std::vector<Tile> const &wanted, TileLoaded const &have, std::vector<Tile> const *current)
{
    std::vector<Tile> missing;
    for (auto const &tile : wanted) {
        if (!have(tile)) {
            missing.push_back(tile);
        }
    }
    auto const in_current = [current](Tile const &tile) {
        return current != nullptr && std::ranges::find(*current, tile) != current->end();
    };
    if (!missing.empty() && std::ranges::all_of(missing, in_current)) {
        return { };
    }
    return missing;
}

u64 next_tile_owner()
{
    static std::atomic<u64> last { 0 };
    return ++last;
}

std::vector<TilePiece> cover(Tile const &tile, TileLoaded const &loaded)
{
    for (u8 up = 1; up <= CoverLevels && up <= tile.zoom; ++up) {
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
        auto const notches = wheel.add(input.wheel, input.time);
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
