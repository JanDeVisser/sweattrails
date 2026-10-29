/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <IO.h>
#include <JSON.h>
#include <Logging.h>

#include <Colors.h>

#include <Zwift.h>
#include <storage/Activity.h>

namespace ST {

namespace {

// OpenStreetMap's tile size, which a world's tiles have too.
constexpr double TileSize = 256;

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

u8 native_zoom(ZwiftWorld const &world, float image_width)
{
    auto const width_at_0 = world_pixel(world.south_east, 0).x - world_pixel(world.north_west, 0).x;
    if (width_at_0 <= 0 || image_width <= 0) {
        return MinWorldZoom;
    }
    auto const zoom = std::round(std::log2(image_width / width_at_0));
    return static_cast<u8>(std::clamp(zoom, static_cast<double>(MinWorldZoom), static_cast<double>(MaxMapZoom)));
}

std::vector<WorldTile> world_tiles(ZwiftWorld const &world, Vector2 image_size, u8 zoom)
{
    auto const nw = world_pixel(world.north_west, zoom);
    auto const se = world_pixel(world.south_east, zoom);
    if (se.x <= nw.x || se.y <= nw.y) {
        return { };
    }
    auto const scale_x = image_size.x / (se.x - nw.x);
    auto const scale_y = image_size.y / (se.y - nw.y);
    auto const first_x = static_cast<u32>(nw.x / TileSize);
    auto const first_y = static_cast<u32>(nw.y / TileSize);
    auto const last_x = static_cast<u32>(std::ceil(se.x / TileSize)) - 1;
    auto const last_y = static_cast<u32>(std::ceil(se.y / TileSize)) - 1;

    std::vector<WorldTile> tiles;
    for (auto y = first_y; y <= last_y; ++y) {
        for (auto x = first_x; x <= last_x; ++x) {
            // The part of the tile the image covers, in world pixels.
            auto const left = std::max(nw.x, static_cast<double>(x) * TileSize);
            auto const top = std::max(nw.y, static_cast<double>(y) * TileSize);
            auto const right = std::min(se.x, static_cast<double>(x + 1) * TileSize);
            auto const bottom = std::min(se.y, static_cast<double>(y + 1) * TileSize);
            tiles.push_back(WorldTile {
                .tile = Tile { .x = x, .y = y, .zoom = zoom },
                .source = Rectangle {
                    .x = static_cast<float>((left - nw.x) * scale_x),
                    .y = static_cast<float>((top - nw.y) * scale_y),
                    .width = static_cast<float>((right - left) * scale_x),
                    .height = static_cast<float>((bottom - top) * scale_y),
                },
                .place = Rectangle {
                    .x = static_cast<float>(left - static_cast<double>(x) * TileSize),
                    .y = static_cast<float>(top - static_cast<double>(y) * TileSize),
                    .width = static_cast<float>(right - left),
                    .height = static_cast<float>(bottom - top),
                },
            });
        }
    }
    return tiles;
}

namespace {

constexpr char const WorldFile[] = "world.json";

std::string color_text(Color color)
{
    return std::format("#{:02x}{:02x}{:02x}{:02x}", color.r, color.g, color.b, color.a);
}

// The part of the image the tile shows, scaled to its place, on the
// background.
std::expected<void, LibCError> write_tile(fs::path const &dir, WorldTile const &world_tile, Image const &image, Color background)
{
    auto const &[tile, source, place] = world_tile;
    auto const width = static_cast<int>(std::lround(place.width));
    auto const height = static_cast<int>(std::lround(place.height));
    auto       out = GenImageColor(static_cast<int>(TileSize), static_cast<int>(TileSize), background);
    if (width > 0 && height > 0 && source.width >= 1 && source.height >= 1) {
        auto part = ImageFromImage(image, source);
        ImageResize(&part, width, height);
        ImageDrawImage(&out, part, static_cast<int>(std::lround(place.x)), static_cast<int>(std::lround(place.y)), WHITE);
        UnloadImage(part);
    }
    auto const tile_dir = ensure_directory(dir / std::format("{}/{}", tile.zoom, tile.x));
    auto const file = tile_dir ? (*tile_dir / std::format("{}.png", tile.y)) : fs::path { };
    auto const written = tile_dir && ExportImage(out, file.c_str());
    UnloadImage(out);
    if (!written) {
        return std::unexpected(LibCError { "Could not write tile {} of `{}`", tile.to_string(), dir.string() });
    }
    return { };
}

// From the native zoom down, each zoom's tiles are cut from the image
// scaled to that zoom, which is half the one before: scaling the whole
// image once a zoom is quicker than scaling each tile from the original.
std::expected<WorldMap, LibCError> cut_tiles(fs::path const &dir, ZwiftWorld const &world, Image &image)
{
    WorldMap map {
        .dir = dir,
        .native_zoom = native_zoom(world, static_cast<float>(image.width)),
        .background = GetImageColor(image, 0, 0),
    };
    for (auto zoom = static_cast<int>(map.native_zoom); zoom >= MinWorldZoom; --zoom) {
        auto const z = static_cast<u8>(zoom);
        auto const nw = world_pixel(world.north_west, z);
        auto const se = world_pixel(world.south_east, z);
        ImageResize(&image, std::max(1, static_cast<int>(std::lround(se.x - nw.x))), std::max(1, static_cast<int>(std::lround(se.y - nw.y))));
        auto const size = Vector2 { .x = static_cast<float>(image.width), .y = static_cast<float>(image.height) };
        for (auto const &tile : world_tiles(world, size, z)) {
            TRY(write_tile(dir, tile, image, map.background));
        }
    }
    auto json = JSONValue::object();
    json.set("native_zoom", JSONValue { static_cast<int>(map.native_zoom) });
    json.set("background", JSONValue { color_text(map.background) });
    TRY(write_file_by_name((dir / WorldFile).string(), json.serialize()));
    return map;
}

}

MapSource world_source(WorldMap const &map)
{
    return MapSource {
        .dir = map.dir,
        .attribution = ZwiftAttribution,
        .downloads = false,
        .max_zoom = map.native_zoom,
        .background = map.background,
    };
}

std::optional<WorldMap> cut_world(fs::path const &cache, ZwiftWorld const &world)
{
    auto const      dir = cache / world.cache_name();
    std::error_code ec { };
    if (!fs::exists(dir / WorldFile, ec)) {
        return { };
    }
    auto const contents = read_file_by_name((dir / WorldFile).string());
    auto const json = contents ? JSONValue::deserialize(*contents) : std::unexpected(JSONError { JSONError::Code::MissingValue, "" });
    if (!json) {
        return { };
    }
    auto const zoom = json->try_get<int>("native_zoom");
    auto const background = json->try_get<std::string>("background");
    auto const color = background ? parse_color(*background) : std::nullopt;
    if (!zoom || *zoom < MinWorldZoom || *zoom > MaxMapZoom || !color) {
        return { };
    }
    return WorldMap { .dir = dir, .native_zoom = static_cast<u8>(*zoom), .background = *color };
}

std::expected<WorldMap, LibCError> prepare_zwift_world(fs::path const &cache, ZwiftWorld const &world)
{
    auto const      image_file = cache / std::format("{}.png", world.cache_name());
    std::error_code ec { };
    if (!fs::exists(image_file, ec)) {
        TRY(download_file(world.image, image_file));
        info(ST, "Downloaded {}'s map, {}", world.name, world.image);
    }
    auto image = LoadImage(image_file.c_str());
    if (image.data == nullptr) {
        // Not kept: it may be an error page, or cut short.
        fs::remove(image_file, ec);
        return std::unexpected(LibCError { "Could not decode {}'s map, `{}`", world.name, world.image });
    }
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    auto const dir = cache / world.cache_name();
    auto       map = cut_tiles(dir, world, image);
    UnloadImage(image);
    return map;
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
