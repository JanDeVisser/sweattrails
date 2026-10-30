#pragma once

#include <map>
#include <memory>
#include <ranges>
#include <set>

#include <raylib.h>

#include <Logging.h>

#include <Table.h>
#include <Widget.h>

#include <Map.h>
#include <Zwift.h>
#include <storage/Activity.h>
#include <widget/DataFields.h>
#include <widget/DisplayedActivity.h>
#include <widget/OverlayPreview.h>

namespace ST {

struct ActivityDataDisplay : public Widget {
    // The most lines of notes drawn under the title.
    static constexpr size_t NotesLines = 3;

    // A field that marks a record when it's clicked, where the last draw()
    // drew it, in screen coordinates.
    struct ClickableField {
        Rectangle area;
        size_t    mark_at;
    };

    std::shared_ptr<DisplayedActivity> activity;
    Texture2D                          sport_icon;
    std::vector<ClickableField>        clickable { };
    Rectangle                          title_area { }; // Where draw() drew the title, in screen coordinates

    explicit ActivityDataDisplay(pWidget const &parent);
    ~ActivityDataDisplay();
    void initialize() override;
    void draw() override;
    void resize() override;
    void process_input() override;
    void set_activity(std::shared_ptr<DisplayedActivity> const &activity);

private:
    float draw_notes(float y);
    void  draw_fields(float y);
    void  draw_field(DataField const &field, float x, float value_x, float y, float height);
};

struct ActivityMap : public Widget {
    // A tile loaded in video memory, and the frame it was last drawn in, to
    // unload the least recently drawn when there are too many.
    struct LoadedTile {
        Texture2D texture { };
        size_t    drawn { 0 };
    };

    // At 256 KB a tile, some 40 MB of video memory.
    static constexpr size_t MaxLoadedTiles = 150;

    std::shared_ptr<DisplayedActivity> activity;
    MapNavigator                       navigator { };
    MapSource                          source { };
    std::string                        waiting_for_world { }; // The cache name of the Zwift world being cut for it
    std::optional<ZwiftWorld>          world_to_cut { };      // Once the view's OSM tiles are asked for
    bool                               has_map { false };
    bool                               view_pending { false }; // The tiles aren't loaded for the view yet
    std::vector<WorldPoint>            track { };              // At zoom 0; NaN without a position
    std::map<Tile, LoadedTile>         tiles { };
    std::set<Tile>                     failed { };   // Not tried again for this activity
    pTileDownload                      download { }; // The view's tiles that weren't cached, while they download
    u64                                map_id { 0 }; // Tiles downloaded for another map are dropped
    size_t                             frame { 0 };
    bool                               reporting_failures { false }; // The status line says tiles failed

    explicit ActivityMap(pWidget const &parent);
    ~ActivityMap();
    void draw() override;
    void resize() override;
    void process_input() override;
    void set_activity(std::shared_ptr<DisplayedActivity> const &activity);
    void add_tile(MapTile const &tile);
    void tiles_done(MapTilesDone const &done);
    // A Zwift world was cut into tiles: if it's the activity's, the map
    // changes to it.
    void world_ready(ZwiftWorldReady const &ready);
    void zoom_by(int steps);
    void show_whole_route();

private:
    void                            choose_source();
    void                            change_source(MapSource new_source);
    void                            view_changed();
    void                            request_tiles();
    [[nodiscard]] bool              load_cached(Tile const &tile);
    [[nodiscard]] LoadedTile const *loaded(Tile const &tile) const;
    void                            load_tile(Tile const &tile, std::string const &png);
    void                            unload_tiles();
    void                            unload_least_recently_drawn();
    void                            draw_tiles();
    void                            draw_track() const;
    void                            draw_attribution() const;
    void                            stop_download();
};

struct ActivityGraph : public Widget {
    std::shared_ptr<DisplayedActivity> activity;
    bool                               dragging { false };
    Image                              image { };   // empty while no graph is shown
    Texture2D                          texture { }; // empty while no graph is shown

    explicit ActivityGraph(pWidget const &parent);
    ~ActivityGraph();
    void initialize() override;
    void draw() override;
    void resize() override;
    void process_input() override;
    void set_activity(std::shared_ptr<DisplayedActivity> const &activity);

private:
    // The lap strip under the graph, half a character high: none without
    // laps.
    [[nodiscard]] float     strip_height() const;
    [[nodiscard]] Rectangle strip_area() const; // In screen coordinates
    void                    draw_strip() const;
    void                    draw_selected_range() const;
};

struct ActivityDisplay : public Layout {
    static constexpr int LapTableRows = 5;

    std::shared_ptr<DisplayedActivity>   activity;
    std::shared_ptr<Layout>              central; // The data panel and the map
    std::shared_ptr<ActivityDataDisplay> data_display;
    std::shared_ptr<ActivityMap>         map;
    std::shared_ptr<OverlayPreview>      preview; // In the place of `central` when it's on
    std::shared_ptr<Table>               laps;
    std::shared_ptr<ActivityGraph>       graph;

    explicit ActivityDisplay(pWidget const &parent);
    void set_activity(Activity const &activity);
    void clear_activity();
    // The activity's laps changed: the table shows them again.
    void               laps_changed();
    [[nodiscard]] bool previewing() const;
    void               show_preview(bool on);
    void               initialize() override;
    void               on_draw() override;

private:
    void show_laps();
};

}
