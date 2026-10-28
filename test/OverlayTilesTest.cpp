/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The overlay's tiles: loaded from the cache, downloaded once, the
 * failures kept, and other owners' downloads ignored. Without a GL
 * context: the textures are made up.
 */

#include <map>
#include <set>
#include <string>
#include <vector>

#include <overlay/OverlayTiles.h>

#include <TestApp.h>

using namespace ST;

namespace {

Tile tile(u32 x)
{
    return Tile { .x = x, .y = 0, .zoom = 15 };
}

// A cache of the tiles in `cached`, a scheduler that keeps what it's
// given, and textures numbered by their PNG, which is "png" and the
// tile's x; "broken" doesn't decode.
struct Fake {
    std::map<Tile, std::string> cached { };
    std::vector<pTileDownload>  scheduled { };
    std::set<unsigned int>      textures { };
    size_t                      unloaded { 0 };

    OverlayTiles make()
    {
        return OverlayTiles {
            [this](Tile const &t) -> std::expected<std::string, LibCError> {
                if (auto it = cached.find(t); it != cached.end()) {
                    return it->second;
                }
                return std::unexpected(LibCError { ENOENT });
            },
            [this](pTileDownload const &download) { scheduled.push_back(download); },
            TileTextures {
                .load = [this](std::string const &png) -> std::optional<Texture2D> {
                    if (png == "broken") {
                        return { };
                    }
                    auto const id = static_cast<unsigned int>(std::stoul(png.substr(3)));
                    textures.insert(id);
                    return Texture2D { .id = id };
                },
                .unload = [this](Texture2D const &texture) {
                    textures.erase(texture.id);
                    ++unloaded; },
            },
        };
    }
};

std::string png(u32 x)
{
    return std::format("png{}", x);
}

void test_cached()
{
    Fake fake;
    fake.cached[tile(1)] = png(1);
    auto tiles = fake.make();
    CHECK(tiles.request({ tile(1) }));
    CHECK(fake.scheduled.empty());
    CHECK_EQ(tiles.texture(tile(1)) ? tiles.texture(tile(1))->id : 0u, 1u);
    CHECK(tiles.texture(tile(2)) == nullptr);
}

// The missing tiles are one download, asked for once, which ends with the
// tiles there and the failures counted, and not asked for again.
void test_download()
{
    Fake fake;
    fake.cached[tile(1)] = png(1);
    auto tiles = fake.make();
    CHECK(!tiles.request({ tile(1), tile(2), tile(3) }));
    CHECK_EQ(fake.scheduled.size(), 1uz);
    if (fake.scheduled.size() != 1) {
        return;
    }
    auto const download = fake.scheduled[0];
    CHECK_EQ(download->tiles, (std::vector { tile(2), tile(3) }));
    CHECK(download->map != 0);

    CHECK(!tiles.request({ tile(1), tile(2), tile(3) }));
    CHECK_EQ(fake.scheduled.size(), 1uz);

    tiles.add_tile(MapTile { .download = download, .tile = tile(2), .png = png(2) });
    CHECK(tiles.texture(tile(2)) != nullptr);
    tiles.tiles_done(MapTilesDone { .download = download, .failed = { tile(3) } });
    CHECK(tiles.request({ tile(1), tile(2), tile(3) }));
    CHECK_EQ(fake.scheduled.size(), 1uz);
    CHECK_EQ(tiles.failed(), 1uz);
}

// Another map's tiles, the app's or another renderer's, aren't these.
void test_another_owner()
{
    Fake fake;
    auto tiles = fake.make();
    CHECK(!tiles.request({ tile(1) }));
    auto other = std::make_shared<TileDownload>();
    other->tiles = { tile(1) };
    other->map = next_tile_owner();
    tiles.add_tile(MapTile { .download = other, .tile = tile(1), .png = png(1) });
    CHECK(tiles.texture(tile(1)) == nullptr);
    tiles.tiles_done(MapTilesDone { .download = other, .failed = { tile(1) } });
    CHECK_EQ(tiles.failed(), 0uz);
    CHECK(!tiles.request({ tile(1) }));
}

// Asking for other tiles drops the download going on; a tile it still
// brings is kept.
void test_replaced()
{
    Fake fake;
    auto tiles = fake.make();
    CHECK(!tiles.request({ tile(1) }));
    CHECK(!tiles.request({ tile(2) }));
    CHECK_EQ(fake.scheduled.size(), 2uz);
    if (fake.scheduled.size() != 2) {
        return;
    }
    CHECK(fake.scheduled[0]->cancelled);
    CHECK(!fake.scheduled[1]->cancelled);
    tiles.add_tile(MapTile { .download = fake.scheduled[0], .tile = tile(1), .png = png(1) });
    CHECK(tiles.texture(tile(1)) != nullptr);
    // All there: the download going on isn't needed any more.
    CHECK(tiles.request({ tile(1) }));
    CHECK(fake.scheduled[1]->cancelled);
}

void test_broken()
{
    Fake fake;
    fake.cached[tile(1)] = "broken";
    auto tiles = fake.make();
    CHECK(tiles.request({ tile(1) }));
    CHECK_EQ(tiles.failed(), 1uz);
    CHECK(fake.scheduled.empty());
}

// Past MaxLoaded, the least recently asked for go, but none asked for now.
void test_unload()
{
    Fake fake;
    for (u32 x = 0; x < 70; ++x) {
        fake.cached[tile(x)] = png(x + 1);
    }
    {
        auto tiles = fake.make();
        for (u32 x = 0; x < 70; ++x) {
            CHECK(tiles.request({ tile(x) }));
        }
        CHECK_EQ(fake.unloaded, 6uz);
        CHECK(tiles.texture(tile(0)) == nullptr);
        CHECK(tiles.texture(tile(6)) != nullptr);
        CHECK(tiles.texture(tile(69)) != nullptr);
    }
    // And the rest when it goes.
    CHECK(fake.textures.empty());
}

void test_destroyed()
{
    Fake fake;
    {
        auto tiles = fake.make();
        CHECK(!tiles.request({ tile(1) }));
    }
    CHECK(fake.scheduled.size() == 1 && fake.scheduled[0]->cancelled);
}

}

int main()
{
    test_cached();
    test_download();
    test_another_owner();
    test_replaced();
    test_broken();
    test_unload();
    test_destroyed();
    return ST::Test::test_result();
}
