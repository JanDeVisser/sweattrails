/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Zwift's worlds, for showing a ride in Zwift on the map of its world
 * rather than on OpenStreetMap. See specs/zwift-maps.md.
 */

#pragma once

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <raylib.h>

#include <Error.h>
#include <JSON.h>

#include <Coordinates.h>
#include <Map.h>

namespace ST {

namespace fs = std::filesystem;

struct Activity;

// A world, as share/zwift-worlds.json has it: Zwift's image of it, and
// where on the earth that image lies. Zwift places its worlds on the earth:
// the made-up ones, such as Watopia, in the Pacific.
struct ZwiftWorld {
    std::string name { };
    std::string image { }; // The image's URL
    Coordinates north_west { };
    Coordinates south_east { };

    // The cache's name for it, from the image's file name, such as
    // "MiniMap_Watopia_2": a new image is a new URL, and so a new cache.
    [[nodiscard]] std::string cache_name() const;
    [[nodiscard]] Box         box() const;

    static Decoded<ZwiftWorld> decode(JSONValue const &json);
};

// A world's map is cut into tiles from this zoom, where a world is a part
// of one tile, up to its native zoom.
constexpr u8 MinWorldZoom = 8;

// The zoom nearest the image's resolution, where an image pixel is about a
// map pixel: 16 for Watopia's 8192 pixels.
u8 native_zoom(ZwiftWorld const &world, float image_width);

// A tile of a world's map: the part of the image it shows, and where that
// goes in the tile. Only an edge tile shows the image in part of it.
struct WorldTile {
    Tile      tile { };
    Rectangle source { }; // In the image's pixels
    Rectangle place { };  // In the tile's 256 pixels
};

// The tiles at `zoom` that the world's image touches. The image is
// stretched between its corners' places on the map: over a world 0.1° high,
// that's the same as Web Mercator to well under a pixel.
std::vector<WorldTile> world_tiles(ZwiftWorld const &world, Vector2 image_size, u8 zoom);

// The worlds in `file`. A world without its four fields is left out, with
// a warning.
std::expected<std::vector<ZwiftWorld>, LibCError> read_zwift_worlds(fs::path const &file);

// The world whose box holds the activity's track, if Zwift made it: an
// outdoor ride in London isn't in Zwift's London.
ZwiftWorld const *zwift_world(std::span<ZwiftWorld const> worlds, Activity const &activity);

}
