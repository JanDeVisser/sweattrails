/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The activity's map: OpenStreetMap tiles with the track over them, which
 * the user can zoom and pan. MapNavigator in Map.h keeps the view; this
 * draws it, and loads and downloads the tiles it needs.
 */

#include <cmath>
#include <format>
#include <limits>

#include <raylib.h>

#include <App.h>

#include <Map.h>
#include <SweatTrails.h>
#include <widget/Activity.h>

namespace ST {

namespace {

constexpr float      TrackWidth = 3.0f;
constexpr char const Attribution[] = "© OpenStreetMap contributors";

}

ActivityMap::ActivityMap(pWidget const &parent)
    : Widget(parent, SizePolicy::Relative, 0.33)
{
    padding = { 10, 10, 10, 10 };
}

ActivityMap::~ActivityMap()
{
    if (download) {
        download->cancelled = true;
    }
    unload_tiles();
}

void ActivityMap::set_activity(std::shared_ptr<DisplayedActivity> const &activity)
{
    stop_download();
    unload_tiles();
    failed.clear();
    track.clear();
    has_map = false;
    ++map_id;
    this->activity = activity;
    if (!this->activity) {
        return;
    }
    auto const &box = this->activity->activity.segment.box;
    if (!box || !navigator.start(*box, viewport.width, viewport.height)) {
        policy = SizePolicy::Hide;
        return;
    }
    policy = SizePolicy::Relative;
    has_map = true;

    // A record without a position is drawn where the one before it was.
    auto const none = std::numeric_limits<double>::quiet_NaN();
    for (auto const &record : this->activity->activity.records) {
        if (record.position && std::abs(record.position->lat) > 0.1 && std::abs(record.position->lon) > 0.1) {
            track.push_back(world_pixel(*record.position, 0));
        } else {
            track.push_back(track.empty() ? WorldPoint { .x = none, .y = none } : track.back());
        }
    }
    view_changed();
}

// The layout gives the map another size: it shows the whole route again,
// unless the user zoomed or panned it.
void ActivityMap::resize()
{
    if (has_map && navigator.resize(viewport.width, viewport.height)) {
        view_changed();
    }
}

void ActivityMap::process_input()
{
    if (!has_map) {
        return;
    }
    auto const area = Rectangle { .x = viewport.x, .y = viewport.y, .width = viewport.width, .height = viewport.height };
    if (navigator.handle(ScrollInput::current(), area)) {
        view_changed();
    }
    if (navigator.dragging) {
        // raylib has no grabbing hand.
        App::the()->request_cursor(MOUSE_CURSOR_RESIZE_ALL);
    }
}

void ActivityMap::zoom_by(int steps)
{
    if (has_map && navigator.zoom_by(steps)) {
        view_changed();
    }
}

void ActivityMap::show_whole_route()
{
    if (has_map && navigator.whole_route()) {
        view_changed();
    }
}

// Loads the view's tiles that are cached, and downloads the others, unless
// they failed before. The download for the view before, if it's still
// going, is dropped: only the tile it's fetching finishes.
void ActivityMap::view_changed()
{
    auto const cached_or_failed = [this](Tile const &tile) {
        if (loaded(tile) != nullptr || failed.contains(tile)) {
            return true;
        }
        if (auto png = tile.get_cached_tile(SweatTrails::the()->storage); png) {
            load_tile(tile, *png);
            return true;
        }
        return false;
    };
    auto missing = missing_tiles(navigator.view, cached_or_failed);
    unload_least_recently_drawn();
    if (download && download->tiles == missing) {
        return;
    }
    if (download) {
        download->cancelled = true;
        download.reset();
    }
    if (missing.empty()) {
        return;
    }
    download = std::make_shared<TileDownload>();
    download->tiles = std::move(missing);
    download->map = map_id;
    SweatTrails::the()->schedule<JobType::DownloadTiles>(download);
}

// A tile downloaded for this activity's map is loaded, also when the view
// it was for is gone: it may be in view again, and it's a placeholder.
void ActivityMap::add_tile(MapTile const &tile)
{
    if (tile.download == nullptr || tile.download->map != map_id || loaded(tile.tile) != nullptr) {
        return;
    }
    load_tile(tile.tile, tile.png);
}

void ActivityMap::tiles_done(MapTilesDone const &done)
{
    if (done.download != download) {
        return;
    }
    download.reset();
    failed.insert(done.failed.begin(), done.failed.end());
    if (!done.failed.empty()) {
        auto const count = done.failed.size();
        SweatTrails::the()->set_status(std::format("Couldn't download {} map {}", count, (count == 1) ? "tile" : "tiles"));
        reporting_failures = true;
    }
}

ActivityMap::LoadedTile const *ActivityMap::loaded(Tile const &tile) const
{
    auto const it = tiles.find(tile);
    return (it != tiles.end()) ? &it->second : nullptr;
}

void ActivityMap::load_tile(Tile const &tile, std::string const &png)
{
    auto image = LoadImageFromMemory(".png", reinterpret_cast<unsigned char const *>(png.data()), static_cast<int>(png.size()));
    if (image.data == nullptr) {
        info(Map, "Could not decode tile {}", tile);
        return;
    }
    tiles[tile] = LoadedTile { .texture = LoadTextureFromImage(image), .drawn = frame };
    UnloadImage(image);
}

void ActivityMap::unload_tiles()
{
    for (auto const &[tile, loaded_tile] : tiles) {
        UnloadTexture(loaded_tile.texture);
    }
    tiles.clear();
}

// The view's tiles, and the ones drawn in place of those that aren't
// loaded yet, stay, whenever they were last drawn.
void ActivityMap::unload_least_recently_drawn()
{
    if (tiles.size() <= MaxLoadedTiles) {
        return;
    }
    std::set<Tile> keep;
    auto const     is_loaded = [this](Tile const &t) { return loaded(t) != nullptr; };
    for (auto const &[tile, screen] : navigator.view.tiles()) {
        if (is_loaded(tile)) {
            keep.insert(tile);
            continue;
        }
        for (auto const &piece : cover(tile, is_loaded)) {
            keep.insert(piece.source);
        }
    }
    std::vector<std::pair<Tile, size_t>> drawn;
    for (auto const &[tile, loaded_tile] : tiles) {
        drawn.emplace_back(tile, loaded_tile.drawn);
    }
    for (auto const &tile : tiles_to_unload(std::move(drawn), keep, MaxLoadedTiles)) {
        UnloadTexture(tiles.at(tile).texture);
        tiles.erase(tile);
    }
}

void ActivityMap::draw()
{
    ++frame;
    if (!activity || !has_map) {
        return;
    }
    DrawRectangleLines(viewport.x - 3, viewport.y - 3, viewport.width + 6, viewport.height + 6, RAYWHITE);
    BeginScissorMode(viewport.x, viewport.y, viewport.width, viewport.height);
    draw_tiles();
    draw_track();
    draw_attribution();
    EndScissorMode();
}

// A tile that isn't loaded is grey, with what there is of the tiles above
// or below it over it, scaled: the view before a zoom, until it arrives.
void ActivityMap::draw_tiles()
{
    auto const full = Rectangle { .x = 0, .y = 0, .width = 256, .height = 256 };
    for (auto const &[tile, screen] : navigator.view.tiles()) {
        auto const destination = Rectangle { .x = viewport.x + screen.x, .y = viewport.y + screen.y, .width = screen.width, .height = screen.height };
        if (auto it = tiles.find(tile); it != tiles.end()) {
            it->second.drawn = frame;
            DrawTexturePro(it->second.texture, full, destination, Vector2 { }, 0.0f, WHITE);
            continue;
        }
        DrawRectangleRec(destination, LIGHTGRAY);
        auto const pieces = cover(tile, [this](Tile const &t) { return loaded(t) != nullptr; });
        for (auto const &[source, source_area, part] : pieces) {
            auto &placeholder = tiles.at(source);
            placeholder.drawn = frame;
            DrawTexturePro(
                placeholder.texture,
                source_area,
                Rectangle { .x = destination.x + part.x, .y = destination.y + part.y, .width = part.width, .height = part.height },
                Vector2 { }, 0.0f, WHITE);
        }
    }
}

// The track in red, the selected range or lap in blue over it, and the
// marked record as a black dot.
void ActivityMap::draw_track() const
{
    if (track.empty()) {
        return;
    }
    auto const           scale = world_size(navigator.view.zoom) / world_size(0);
    std::vector<Vector2> points;
    points.reserve(track.size());
    for (auto const &p : track) {
        auto const screen = navigator.view.to_screen(WorldPoint { .x = p.x * scale, .y = p.y * scale });
        points.push_back(Vector2 { .x = viewport.x + screen.x, .y = viewport.y + screen.y });
    }

    // Points less than a pixel from the one before are left out.
    auto const draw_path = [&points](size_t from, size_t to, Color color) {
        auto previous = points[from];
        for (auto ix = from; ix < to; ++ix) {
            auto const &p = points[ix];
            if (std::isnan(p.x) || std::isnan(p.y)) {
                continue;
            }
            if (std::isnan(previous.x) || std::isnan(previous.y)) {
                previous = p;
                continue;
            }
            if (std::abs(previous.x - p.x) > 1 || std::abs(previous.y - p.y) > 1) {
                DrawLineEx(previous, p, TrackWidth, color);
                DrawCircleV(p, TrackWidth / 2, color);
                previous = p;
            }
        }
    };
    draw_path(0, points.size(), RED);
    if (activity->segment && activity->segment->min < points.size()) {
        draw_path(activity->segment->min, std::min(activity->segment->max + 1, points.size()), BLUE);
    }
    if (activity->mark && *activity->mark < points.size()) {
        DrawCircleV(points[*activity->mark], TrackWidth, BLACK);
    }
}

// OSM's license asks for it on the map.
void ActivityMap::draw_attribution() const
{
    auto const size = App::measure_text(Attribution, FontSize::Small);
    auto const x = viewport.width - size.x - 6;
    auto const y = viewport.height - size.y - 4;
    draw_rectangle_no_normalize(x - 4, y - 2, size.x + 8, size.y + 4, Fade(BLACK, 0.5f));
    render_text(x, y, Attribution, FontSize::Small, RAYWHITE);
}

// Drops the tiles still waiting to be downloaded, and the status line
// message about the ones that failed.
void ActivityMap::stop_download()
{
    if (download) {
        download->cancelled = true;
        download.reset();
    }
    if (reporting_failures) {
        SweatTrails::the()->set_status("");
        reporting_failures = false;
    }
}

}
