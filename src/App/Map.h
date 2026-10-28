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
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <raylib.h>

#include <Error.h>
#include <Expected.h>
#include <Logging.h>

#include <Scroll.h>

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

    auto operator<=>(Tile const &) const = default;
};

// OSM's licence asks for this wherever its tiles are shown.
constexpr char const OsmAttribution[] = "© OpenStreetMap contributors";

// The map zooms in to this OSM level, street level. GPS isn't more precise,
// and OSM's server stops at 19.
constexpr u8 MaxMapZoom = 18;

// The whole route is shown at this level at most, as before zooming.
constexpr u8 MaxFitZoom = 16;

// The map zooms out this many levels past the whole route, for context.
constexpr u8 ZoomOutPastFit = 2;

// A point's place on the map: its Web Mercator position in pixels, from 0
// to 256 << zoom. Doubles, since at zoom 18 that's up to 67 million, which a
// float doesn't hold to the pixel.
struct WorldPoint {
    double x { 0 };
    double y { 0 };

    bool operator==(WorldPoint const &) const = default;
};

// The width and height of the world, in pixels, at a zoom level.
double world_size(u8 zoom);

WorldPoint  world_pixel(Coordinates const &coordinates, u8 zoom);
Coordinates world_coordinates(WorldPoint const &point, u8 zoom);

// A tile, and where it goes, relative to the map's top left corner.
struct PlacedTile {
    Tile      tile { };
    Rectangle screen { };
};

// What the map shows: the world point at the middle of a viewport of
// width x height pixels, at a zoom level.
struct MapView {
    u8         zoom { 0 };
    WorldPoint center { };
    float      width { 0 };
    float      height { 0 };

    // The whole route, with 10% to spare, at the highest zoom up to
    // MaxFitZoom where it fits. Nothing if it doesn't fit at any zoom.
    static std::optional<MapView> fit(Box const &route, float width, float height);

    // Relative to the viewport's top left corner.
    [[nodiscard]] Vector2    to_screen(WorldPoint const &point) const;
    [[nodiscard]] WorldPoint to_world(Vector2 const &screen) const;

    // `steps` levels in, or out if negative, keeping the world point under
    // `around` where it is.
    [[nodiscard]] MapView zoomed(int steps, Vector2 const &around) const;
    // The map moved by `delta` on screen, as when dragged.
    [[nodiscard]] MapView panned(Vector2 const &delta) const;
    [[nodiscard]] MapView resized(float new_width, float new_height) const;

    // The tiles that cover the view. Tiles past the world's edges are left
    // out.
    [[nodiscard]] std::vector<PlacedTile> tiles() const;
};

// Part of a loaded tile's texture, drawn over part of a tile that isn't
// loaded. `destination` is relative to the tile, in its 256 x 256 pixels.
struct TilePiece {
    Tile      source { };
    Rectangle source_area { };
    Rectangle destination { };
};

using TileLoaded = std::function<bool(Tile const &)>;

// What to draw for a tile that isn't loaded yet: the part of its nearest
// loaded ancestor, up to three levels up, that covers it, as after zooming
// in; or else those of its four children that are loaded, as after zooming
// out. Nothing means grey.
std::vector<TilePiece> cover(Tile const &tile, TileLoaded const &loaded);

// The map's zooming and panning, with the limits, and the mouse input that
// drives them. Input is made up in tests, as for Scrollbar.
struct MapNavigator {
    Box          route { };
    MapView      view { };
    u8           fit_zoom { 0 };  // The whole route's zoom level
    bool         moved { false }; // The user zoomed or panned
    bool         dragging { false };
    Vector2      drag_from { }; // The mouse, in screen coordinates
    WheelNotches wheel { };

    // Shows the whole route. False if it doesn't fit at any zoom.
    bool start(Box const &new_route, float width, float height);

    // The wheel over the viewport zooms around the mouse; a press there
    // starts a drag, which pans until the button is let go. Returns whether
    // the view changed.
    bool handle(ScrollInput const &input, Rectangle const &viewport);

    // Zooms around `around`, relative to the viewport, or its middle.
    bool zoom_by(int steps, std::optional<Vector2> around = { });
    bool whole_route();
    // The whole route again at the new size, unless the user moved the
    // map: then the same center and zoom.
    bool resize(float width, float height);

    [[nodiscard]] u8 min_zoom() const;
    [[nodiscard]] u8 max_zoom() const;

private:
    void clamp_center();
};

// The tiles of a map that weren't cached, for the job thread to download.
// The UI thread sets `cancelled` when the map shows another view, or
// another activity. `map` says which activity's map it's for.
struct TileDownload {
    std::vector<Tile> tiles { };
    u64               map { 0 };
    std::atomic<bool> cancelled { false };
};

using pTileDownload = std::shared_ptr<TileDownload>;

// Sent to the UI thread for each tile that downloaded.
struct MapTile {
    pTileDownload download { };
    Tile          tile { };
    std::string   png { };
};

// Sent to the UI thread when a download ends: all its tiles were tried, or
// it was cancelled. The tiles that failed are in either case, so that the
// map doesn't ask for them again.
struct MapTilesDone {
    pTileDownload     download { };
    std::vector<Tile> failed { };
};

using TileFetcher = std::function<std::expected<std::string, LibCError>(Tile const &)>;
using TileReceiver = std::function<void(Tile const &tile, std::string png)>;

// Fetches the download's tiles one at a time, and hands each one that
// arrives to on_tile. Stops when the download is cancelled: a tile that is
// being fetched then isn't handed on. Returns the tiles that failed.
std::vector<Tile> fetch_tiles(TileDownload const &download, TileFetcher const &fetch, TileReceiver const &on_tile);

// The loaded tiles to unload to get down to `max`: the least recently
// drawn, each with the frame it was drawn in, but none of `keep`, the
// view's tiles and the placeholders it shows. More than `max` stay if they
// are all kept.
std::vector<Tile> tiles_to_unload(std::vector<std::pair<Tile, size_t>> loaded, std::set<Tile> const &keep, size_t max);

// The view's tiles that `have` says aren't there yet, for a download.
std::vector<Tile> missing_tiles(MapView const &view, TileLoaded const &have);

// What a change of the view does to the download going on, of `current`
// (none if null): nothing, if it has all the view's missing tiles, as when
// a drag moves less than a tile; else it's replaced by a download of the
// missing tiles. If there are none, that only cancels the one going on.
std::optional<std::vector<Tile>> next_download(MapView const &view, TileLoaded const &have, std::vector<Tile> const *current);

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
