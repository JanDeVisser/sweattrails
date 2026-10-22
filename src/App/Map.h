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

#include <atomic>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <raylib.h>

#include <Error.h>
#include <Expected.h>
#include <Logging.h>

#include <Coordinates.h>

// #include <Date.h> // for the integer aliases

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
    [[nodiscard]] std::expected<std::string, LibCError> get_cached_tile(Storage const &storage) const;
    [[nodiscard]] std::string                           to_string() const;

    // The cached tile, or else downloads it. Only call this on the job
    // thread: downloads are slow, and use a curl handle that isn't shared
    // between threads.
    [[nodiscard]] std::expected<std::string, LibCError> get_tile(Storage const &storage) const;

    bool operator==(Tile const &) const = default;
};

struct Map {
    u8        zoom { 0 };
    u32       x { 0 };
    u32       y { 0 };
    u16       width { 0 };
    u16       height { 0 };
    u16       columns { 0 };
    u16       rows { 0 };
    Rectangle rectangle;
    u16       num_tiles { 0 };

    static std::expected<Map, LibCError> init(Box const &b, u16 width, u16 height);

    [[nodiscard]] Tile        tile(size_t ix) const;
    [[nodiscard]] Tile        tile_xy(u32 x, u32 y) const;
    [[nodiscard]] Coordinates coordinates(float32 x, float32 y) const;
};

struct MissingTile {
    size_t index { 0 }; // in the map's grid, as for Map::tile()
    Tile   tile { };
};

// The tiles of a map that weren't cached, for the job thread to download.
// The UI thread sets `cancelled` when it shows another map.
struct TileDownload {
    std::vector<MissingTile> tiles { };
    std::atomic<bool>        cancelled { false };
};

using pTileDownload = std::shared_ptr<TileDownload>;

// Sent to the UI thread for each tile that downloaded.
struct MapTile {
    pTileDownload download { };
    size_t        index { 0 };
    std::string   png { };
};

// Sent to the UI thread when all of a download's tiles have been tried,
// unless it was cancelled.
struct MapTilesDone {
    pTileDownload download { };
    size_t        failed { 0 };
};

using TileFetcher = std::function<std::expected<std::string, LibCError>(Tile const &)>;
using TileReceiver = std::function<void(size_t index, std::string png)>;

// Fetches the download's tiles one at a time, and hands each one that
// arrives to on_tile. Stops when the download is cancelled: a tile that is
// being fetched then isn't handed on. Returns the number of tiles that
// failed.
size_t fetch_tiles(TileDownload const &download, TileFetcher const &fetch, TileReceiver const &on_tile);

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
