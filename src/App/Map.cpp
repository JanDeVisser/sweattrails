/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/map.zig.
 *
 * zig fetches tiles with std.http.Client. There is no HTTP client in the C++
 * code base, so Tile::get_tile shells out to curl (via ST::Process) and lets it
 * write the PNG straight into the tile cache. Swap that out once there is a
 * real client.
 */

#include "Coordinates.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <format>
#include <numbers>
#include <system_error>

#include <IO.h>
#include <Logging.h>
#include <Process.h>

#include <Map.h>
#include <storage/Storage.h>

namespace ST {

namespace fs = std::filesystem;

namespace {

constexpr float32 PI = std::numbers::pi_v<float32>;

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
    auto const tile_file = (tile_dir / std::format("{}.png", y)).string();

    Process<> curl { "curl", "-s", "-f", "-A", "sweattrails/1.0", url };
    curl.stdout_file = tile_file;
    auto const exit_code = TRY_EVAL(curl.execute());
    if (exit_code != 0) {
        fs::remove(tile_file, ec);
        return std::unexpected(LibCError { std::format("Error fetching tile `{}`: curl exited with {}", url, exit_code) });
    }
    trace(Map, "fetched tile {}", url);
    return get_cached_tile(storage);
}

std::expected<Map, LibCError> Map::init(Storage const &storage, Box const &b, u8 width, u8 height)
{
    assert(width > 0 && width <= 8);
    assert(height > 0 && height <= 4);
    u16 const  columns = 2 * width + 1;
    u16 const  rows = 2 * height + 1;
    u8 const   min_dim = std::min(width, height);
    auto const mid = b.center();
    for (auto zoom = static_cast<u8>(16 - min_dim - 1); zoom > 0; --zoom) {
        auto const mid_tile = Tile::for_coordinates(mid, zoom);
        auto const tile_box = mid_tile.box();
        if (tile_box.width() <= b.width() * 1.1f || tile_box.height() <= b.height() * 1.1f) {
            continue;
        }
        auto const map_zoom = static_cast<u8>(zoom + min_dim - 1);
        auto const t = Tile::for_coordinates(mid, map_zoom);
        Map        ret {
                   .zoom = map_zoom,
                   .x = t.x - width,
                   .y = t.y - height,
                   .width = width,
                   .height = height,
                   .columns = columns,
                   .rows = rows,
                   .num_tiles = static_cast<u16>(columns * rows),
                   .tiles = { },
        };
        ret.tiles.reserve(ret.num_tiles);
        for (u32 i = 0; i < ret.num_tiles; ++i) {
            Tile const tile_ix {
                .x = ret.x + i % columns,
                .y = ret.y + i / columns,
                .zoom = map_zoom,
            };
            ret.tiles.emplace_back(TRY_EVAL(tile_ix.get_tile(storage)));
        }
        return ret;
    }
    return std::unexpected(LibCError { "Could not find a zoom level for the requested box" });
}

Tile Map::tile(size_t ix) const
{
    assert(ix < num_tiles);
    return tile_xy(static_cast<u32>(ix % columns), static_cast<u32>(ix / columns));
}

Tile Map::tile_xy(u32 tile_x, u32 tile_y) const
{
    assert(tile_x < columns && tile_y < rows);
    return Tile { .x = x + tile_x, .y = y + tile_y, .zoom = zoom };
}

Box Map::box() const
{
    return sub_box(0, 0, rows - 1u, columns - 1u);
}

Box Map::sub_box(u32 nw_x, u32 nw_y, u32 height, u32 width) const
{
    assert(nw_x < columns - 1u);
    assert(nw_y < rows - 1u);
    assert(width < columns);
    assert(height < rows);
    assert(nw_x + width < columns && nw_y + height < rows);
    auto const t_sw = tile_xy(nw_x, nw_y + height);
    auto const t_ne = tile_xy(nw_x + width, nw_y);
    return Box { .sw = t_sw.box().sw, .ne = t_ne.box().ne };
}

Coordinates Map::coordinates(float32 map_x, float32 map_y) const
{
    assert(map_x >= 0 && map_x <= static_cast<float32>(columns));
    assert(map_y >= 0 && map_y <= static_cast<float32>(rows));
    auto const t = tile_xy(static_cast<u32>(std::trunc(map_x)), static_cast<u32>(std::trunc(map_y)));
    auto const b = t.box();
    return Coordinates {
        .lat = b.sw.lat + (1 - (map_y - std::trunc(map_y))) * (b.ne.lat - b.sw.lat),
        .lon = b.sw.lon + (map_x - std::trunc(map_x)) * (b.ne.lon - b.sw.lon),
    };
}

}
