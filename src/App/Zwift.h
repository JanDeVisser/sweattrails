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

#include <Error.h>
#include <JSON.h>

#include <Coordinates.h>

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

// The worlds in `file`. A world without its four fields is left out, with
// a warning.
std::expected<std::vector<ZwiftWorld>, LibCError> read_zwift_worlds(fs::path const &file);

// The world whose box holds the activity's track, if Zwift made it: an
// outdoor ride in London isn't in Zwift's London.
ZwiftWorld const *zwift_world(std::span<ZwiftWorld const> worlds, Activity const &activity);

}
