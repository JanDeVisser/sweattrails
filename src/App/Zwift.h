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
#include <optional>
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

// Where a world lies on the map at a zoom, in whole map pixels: its image
// is scaled to this size to be cut into that zoom's tiles, one image pixel
// to a map pixel. Over a world 0.1° high, stretching the image between its
// corners is the same as Web Mercator to well under a pixel.
struct WorldArea {
    i64 left { 0 };
    i64 top { 0 };
    int width { 0 };
    int height { 0 };
};

WorldArea world_area(ZwiftWorld const &world, u8 zoom);

// A tile of a world's map: the part of the image, scaled to the zoom, it
// shows, and where that goes in the tile, in whole pixels, so that each of
// the image's pixels is in one tile. Only an edge tile shows the image in
// part of it.
struct WorldTile {
    Tile      tile { };
    Rectangle source { }; // In the scaled image's pixels
    Rectangle place { };  // In the tile's 256 pixels, as large as `source`
};

// The tiles at `zoom` that the world's area touches.
std::vector<WorldTile> world_tiles(ZwiftWorld const &world, u8 zoom);

// A world cut into tiles, in its cache: `<cache>/<cache name>/`, with its
// tiles in `<zoom>/<x>/<y>.png` as OpenStreetMap's are.
struct WorldMap {
    fs::path dir { };
    u8       native_zoom { 0 };
    Color    background { }; // The image's top left pixel: the sea
};

// The world's map in `cache`, if its tiles have all been cut: its
// `world.json`, written last, says so.
std::optional<WorldMap> cut_world(fs::path const &cache, ZwiftWorld const &world);

// Downloads the world's image to `<cache>/<cache name>.png`, unless it's
// there, and cuts it into tiles from MinWorldZoom to its native zoom, on
// the background where a tile goes past the image. Only call this on the
// job thread: it downloads, and takes some seconds.
std::expected<WorldMap, LibCError> prepare_zwift_world(fs::path const &cache, ZwiftWorld const &world);

// Zwift's images are Zwift's.
constexpr char const ZwiftAttribution[] = "Map © Zwift";

// The activity map's source for a world's tiles.
MapSource world_source(WorldMap const &map);

// Sent to the UI thread when prepare_zwift_world() is done with a world:
// its map, or else why not.
struct ZwiftWorldReady {
    std::string             cache_name { };
    std::optional<WorldMap> map { };
    std::string             error { };
};

// The worlds in `file`. A world without its four fields is left out, with
// a warning.
std::expected<std::vector<ZwiftWorld>, LibCError> read_zwift_worlds(fs::path const &file);

// The world whose box holds the activity's track, if Zwift made it: an
// outdoor ride in London isn't in Zwift's London.
ZwiftWorld const *zwift_world(std::span<ZwiftWorld const> worlds, Activity const &activity);

}
