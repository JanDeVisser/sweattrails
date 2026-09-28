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

#include "raylib.h"
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
    [[nodiscard]] float                                 north() const { return box().north(); }
    [[nodiscard]] float                                 south() const { return box().south(); }
    [[nodiscard]] float                                 east() const { return box().east(); }
    [[nodiscard]] float                                 west() const { return box().west(); }
    [[nodiscard]] bool                                  contains(Coordinates const &) const;
    [[nodiscard]] std::expected<std::string, LibCError> get_tile(Storage const &storage) const;
    [[nodiscard]] std::string                           to_string() const;

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
    Rectangle                rectangle;
    u16                      num_tiles { 0 };
    std::vector<std::string> tiles { };

    static std::expected<Map, LibCError> init(Storage const &storage, Box const &b, u16 width, u16 height);

    [[nodiscard]] Tile        tile(size_t ix) const;
    [[nodiscard]] Tile        tile_xy(u32 x, u32 y) const;
    [[nodiscard]] Coordinates coordinates(float32 x, float32 y) const;
};

}

template<>
struct std::formatter<ST::Tile> : std::formatter<std::string> {
    template<class FmtContext>
    FmtContext::iterator format(ST::Tile const &val, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << val.to_string();
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};
