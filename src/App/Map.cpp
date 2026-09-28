/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cassert>
#include <cmath>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
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
    return { };
}

std::expected<std::string, LibCError> Tile::get_tile(Storage const &storage) const
{
    if (auto cached = get_cached_tile(storage); cached) {
        return cached;
    }
    auto const url = std::format("https://tile.openstreetmap.org/{}/{}/{}.png", zoom, x, y);

    std::error_code ec { };
    auto const      tile_dir = storage.tiles_dir / std::format("{}/{}", zoom, x);
    fs::create_directories(tile_dir, ec);
    if (ec) {
        return std::unexpected(LibCError { ec.message() });
    }
    auto const tile_file = tile_dir / std::format("{}.png", y);

    auto const exit_code = download(url, tile_file);
    if (!exit_code.has_value()) {
        fs::remove(tile_file, ec);
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
    trace(Map, "fetched tile {}", url);
    return get_cached_tile(storage);
}

std::string Tile::to_string() const
{
    return std::format("zoom {}/x: {} y: {}/box: `{}`", zoom, x, y, box());
}

/**
 * Initializes a Map to display by calculating coordinates and downloading
 * OSM tiles.
 *
 * @param storage Reference to the application storage context
 * @param b Bounding box in real world map coordinates of the map to be
 * displayed.
 * @param width Width in pixels of the Map image area
 * @param height Height in pixels of the Map image area
 */
std::expected<Map, LibCError> Map::init(Storage const &storage, Box const &b, u16 width, u16 height)
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
        Map ret {
            .zoom = zoom,
            .x = x0,
            .y = y0,
            .width = width,
            .height = height,
            .columns = columns,
            .rows = rows,
            .rectangle = rect,
            .num_tiles = static_cast<u16>(columns * rows),
            .tiles = { },
        };
        ret.tiles.reserve(ret.num_tiles);
        for (u32 i = 0; i < ret.num_tiles; ++i) {
            Tile const tile_ix {
                .x = ret.x + (i % columns),
                .y = ret.y + (i / columns),
                .zoom = zoom,
            };
            ret.tiles.emplace_back(TRY_EVAL(tile_ix.get_tile(storage)));
        }
        return ret;
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
}
