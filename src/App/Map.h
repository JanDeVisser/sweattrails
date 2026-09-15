/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/map.zig.
 *
 * `Tile.zoom` is a u5 in zig; here it is a u8 with the same 0..31 contract.
 * The arena that `Map` owned is replaced by a vector of owning strings.
 */

#pragma once

#include <expected>
#include <string>

#include <Coordinates.h>
#include <Error.h>
#include <Expected.h>
#include <Logging.h>

// #include "Date.h" // for the integer aliases

namespace ST {

struct Storage;

struct Tile {
    u32 x { 0 };
    u32 y { 0 };
    u8  zoom { 0 };

    static Tile for_coordinates(Coordinates const &pos, u8 zoom);

    [[nodiscard]] Box                                   box() const;
    [[nodiscard]] Coordinates                           coordinates() const;
    [[nodiscard]] bool                                  contains(Coordinates const &) const;
    [[nodiscard]] std::expected<std::string, LibCError> get_tile(Storage const &storage) const;

    bool operator==(Tile const &) const = default;

private:
    [[nodiscard]] std::expected<std::string, LibCError> get_cached_tile(Storage const &storage) const;
};

struct Map {
    u8                       zoom { 0 };
    u32                      x { 0 };
    u32                      y { 0 };
    u16                      width { 0 };
    u16                      height { 0 };
    u16                      columns { 0 };
    u16                      rows { 0 };
    u16                      num_tiles { 0 };
    std::vector<std::string> tiles { };

    static std::expected<Map, LibCError> init(Storage const &storage, Box const &b, u8 width, u8 height);

    [[nodiscard]] Tile tile(size_t ix) const;
    [[nodiscard]] Tile tile_xy(u32 x, u32 y) const;
    [[nodiscard]] Box  box() const;
    [[nodiscard]] Box  sub_box(u32 nw_x, u32 nw_y, u32 height, u32 width) const;
    // x in [0..map.columns], y in [0..map.rows]
    [[nodiscard]] Coordinates coordinates(float32 x, float32 y) const;
};

}
