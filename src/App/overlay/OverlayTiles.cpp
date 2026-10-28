/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <Logging.h>

#include <overlay/OverlayTiles.h>

namespace ST {

TileTextures gl_tile_textures()
{
    return TileTextures {
        .load = [](std::string const &png) -> std::optional<Texture2D> {
            auto image = LoadImageFromMemory(".png", reinterpret_cast<unsigned char const *>(png.data()), static_cast<int>(png.size()));
            if (image.data == nullptr) {
                return { };
            }
            auto const ret = LoadTextureFromImage(image);
            UnloadImage(image);
            SetTextureFilter(ret, TEXTURE_FILTER_BILINEAR);
            return ret;
        },
        .unload = [](Texture2D const &texture) { UnloadTexture(texture); },
    };
}

OverlayTiles::OverlayTiles(TileFetcher cached, TileScheduler schedule, TileTextures textures)
    : m_cached(std::move(cached))
    , m_schedule(std::move(schedule))
    , m_textures(std::move(textures))
{
}

OverlayTiles::~OverlayTiles()
{
    if (m_download) {
        m_download->cancelled = true;
    }
    for (auto const &[tile, loaded] : m_loaded) {
        m_textures.unload(loaded.texture);
    }
}

bool OverlayTiles::request(std::vector<Tile> const &tiles)
{
    ++m_requests;
    auto const have = [this](Tile const &tile) {
        if (auto it = m_loaded.find(tile); it != m_loaded.end()) {
            it->second.used = m_requests;
            return true;
        }
        if (m_failed.contains(tile)) {
            return true;
        }
        if (auto png = m_cached(tile); png) {
            // A cached tile that doesn't decode would only fail again.
            if (!load(tile, *png)) {
                m_failed.insert(tile);
            }
            return true;
        }
        if (m_failed_in_a_row >= GiveUpAfter) {
            m_failed.insert(tile);
            return true;
        }
        return false;
    };
    auto const next = next_download(tiles, have, m_download ? &m_download->tiles : nullptr);
    unload_least_recently_used(tiles);
    if (!next) {
        return false;
    }
    if (m_download) {
        m_download->cancelled = true;
        m_download.reset();
    }
    if (next->empty()) {
        return true;
    }
    m_download = std::make_shared<TileDownload>();
    m_download->tiles = *next;
    m_download->map = m_owner;
    m_schedule(m_download);
    return false;
}

void OverlayTiles::reset()
{
    if (m_download) {
        m_download->cancelled = true;
        m_download.reset();
    }
    m_failed.clear();
    m_failed_in_a_row = 0;
}

Texture2D const *OverlayTiles::texture(Tile const &tile) const
{
    auto const it = m_loaded.find(tile);
    return (it != m_loaded.end()) ? &it->second.texture : nullptr;
}

// Also a tile of a download a later request replaced: it may be asked for
// again.
void OverlayTiles::add_tile(MapTile const &tile)
{
    if (tile.download == nullptr || tile.download->map != m_owner || m_loaded.contains(tile.tile)) {
        return;
    }
    if (!load(tile.tile, tile.png)) {
        m_failed.insert(tile.tile);
        return;
    }
    m_failed_in_a_row = 0;
}

// The tiles that failed aren't asked for again: a server that refuses a
// tile shouldn't be asked for it on every frame.
void OverlayTiles::tiles_done(MapTilesDone const &done)
{
    if (done.download == nullptr || done.download->map != m_owner) {
        return;
    }
    m_failed.insert(done.failed.begin(), done.failed.end());
    auto const before = m_failed_in_a_row;
    m_failed_in_a_row += done.failed.size();
    if (before < GiveUpAfter && m_failed_in_a_row >= GiveUpAfter) {
        warning(Map, "{} map tiles in a row couldn't be downloaded: downloading no more for the overlay", m_failed_in_a_row);
    }
    if (done.download == m_download) {
        m_download.reset();
    }
}

bool OverlayTiles::load(Tile const &tile, std::string const &png)
{
    auto const texture = m_textures.load(png);
    if (!texture) {
        info(Map, "Could not decode tile {}", tile);
        return false;
    }
    m_loaded[tile] = Loaded { .texture = *texture, .used = m_requests };
    return true;
}

void OverlayTiles::unload_least_recently_used(std::vector<Tile> const &keep)
{
    if (m_loaded.size() <= MaxLoaded) {
        return;
    }
    std::vector<std::pair<Tile, size_t>> used;
    for (auto const &[tile, loaded] : m_loaded) {
        used.emplace_back(tile, loaded.used);
    }
    for (auto const &tile : tiles_to_unload(std::move(used), std::set<Tile> { keep.begin(), keep.end() }, MaxLoaded)) {
        m_textures.unload(m_loaded.at(tile).texture);
        m_loaded.erase(tile);
    }
}

}
