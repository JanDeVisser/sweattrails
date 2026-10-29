/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <IO.h>
#include <JSON.h>
#include <Logging.h>

#include <Zwift.h>
#include <storage/Activity.h>

namespace ST {

namespace {

// A corner is `[latitude, longitude]`.
Decoded<Coordinates> decode_corner(JSONValue const &json, std::string_view key)
{
    auto const corner = TRY_EVAL(json.try_get_array<double>(key));
    if (corner.size() != 2) {
        return std::unexpected(JSONError { JSONError::Code::TypeMismatch, std::format("`{}` isn't [latitude, longitude]", key) });
    }
    return Coordinates { .lat = static_cast<float32>(corner[0]), .lon = static_cast<float32>(corner[1]) };
}

}

std::string ZwiftWorld::cache_name() const
{
    auto path = std::string_view { image };
    if (auto const query = path.find_first_of("?#"); query != std::string_view::npos) {
        path = path.substr(0, query);
    }
    if (auto const slash = path.rfind('/'); slash != std::string_view::npos) {
        path = path.substr(slash + 1);
    }
    if (auto const dot = path.rfind('.'); dot != std::string_view::npos) {
        path = path.substr(0, dot);
    }
    return std::string { path };
}

Box ZwiftWorld::box() const
{
    return Box {
        .sw = Coordinates { .lat = south_east.lat, .lon = north_west.lon },
        .ne = Coordinates { .lat = north_west.lat, .lon = south_east.lon },
    };
}

Decoded<ZwiftWorld> ZwiftWorld::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "ZwiftWorld"));
    }
    return ZwiftWorld {
        .name = TRY_EVAL(json.try_get<std::string>("name")),
        .image = TRY_EVAL(json.try_get<std::string>("image")),
        .north_west = TRY_EVAL(decode_corner(json, "north_west")),
        .south_east = TRY_EVAL(decode_corner(json, "south_east")),
    };
}

std::expected<std::vector<ZwiftWorld>, LibCError> read_zwift_worlds(fs::path const &file)
{
    auto const contents = TRY_EVAL(read_file_by_name(file.string()));
    auto const json = JSONValue::deserialize(contents);
    if (!json) {
        return std::unexpected(LibCError { "`{}` isn't JSON: {}", file.string(), json.error().to_string() });
    }
    if (!json->is_array()) {
        return std::unexpected(LibCError { "`{}` isn't a list of worlds", file.string() });
    }
    std::vector<ZwiftWorld> worlds;
    for (auto ix = 0u; ix < json->size(); ++ix) {
        auto world = ZwiftWorld::decode((*json)[ix]);
        if (!world) {
            warning(ST, "Leaving out world {} of `{}`: {}", ix + 1, file.string(), world.error().to_string());
            continue;
        }
        worlds.push_back(std::move(*world));
    }
    return worlds;
}

ZwiftWorld const *zwift_world(std::span<ZwiftWorld const> worlds, Activity const &activity)
{
    auto const &track = activity.segment.box;
    if (activity.manufacturer != FIT::manufacturer::zwift || !track) {
        return nullptr;
    }
    auto const it = std::ranges::find_if(worlds, [&track](ZwiftWorld const &world) {
        return world.box().contains(*track);
    });
    return (it != worlds.end()) ? &*it : nullptr;
}

}
