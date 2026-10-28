/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The OpenStreetMap tiles of one renderer's maps, the preview's or a
 * render's: loaded from the cache at once, or else downloaded on the job
 * thread, one download at a time. See specs/overlay-map-tiles.md.
 */

#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <raylib.h>

#include <Map.h>

namespace ST {

// Tests have no GL context: they make up the textures.
struct TileTextures {
    std::function<std::optional<Texture2D>(std::string const &png)> load;
    std::function<void(Texture2D const &)>                          unload;
};

// From a tile's PNG, bilinear, since a map draws tiles scaled.
TileTextures gl_tile_textures();

using TileScheduler = std::function<void(pTileDownload const &)>;

struct OverlayTiles {
    OverlayTiles(TileFetcher cached, TileScheduler schedule, TileTextures textures = gl_tile_textures());
    ~OverlayTiles(); // Unloads the textures, and drops the download
    OverlayTiles(OverlayTiles const &) = delete;
    OverlayTiles &operator=(OverlayTiles const &) = delete;

    // Loads the cached ones of `tiles`, and downloads the others, unless
    // they failed before. The download going on is replaced, unless it has
    // all of them. True when all are loaded or failed. After GiveUpAfter
    // tiles failed in a row, it downloads no more: a server that refuses
    // them, or a network that hangs, would only be asked again for every
    // frame's new tiles. Those count as failed.
    bool request(std::vector<Tile> const &tiles);
    // Drops the download going on, and forgets the tiles that failed, to
    // try them again, and downloads again after giving up: for another
    // activity, when the network may be back.
    void reset();
    // Null if it's not loaded.
    [[nodiscard]] Texture2D const *texture(Tile const &tile) const;

    // From the job thread's notifications. Tiles downloaded for another
    // owner are ignored.
    void add_tile(MapTile const &tile);
    void tiles_done(MapTilesDone const &done);

    [[nodiscard]] size_t failed() const { return m_failed.size(); }

    // Many times what a frame has in view.
    static constexpr size_t MaxLoaded = 64;
    static constexpr size_t GiveUpAfter = 5;

private:
    struct Loaded {
        Texture2D texture { };
        size_t    used { 0 }; // The request it was last in
    };

    TileFetcher            m_cached;
    TileScheduler          m_schedule;
    TileTextures           m_textures;
    u64                    m_owner { next_tile_owner() };
    std::map<Tile, Loaded> m_loaded { };
    std::set<Tile>         m_failed { };
    pTileDownload          m_download { };
    size_t                 m_requests { 0 };
    size_t                 m_failed_in_a_row { 0 }; // Since a tile last arrived

    bool load(Tile const &tile, std::string const &png);
    void unload_least_recently_used(std::vector<Tile> const &keep);
};

}
