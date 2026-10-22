#pragma once

#include <memory>
#include <ranges>

#include <raylib.h>

#include <Logging.h>

#include <Widget.h>

#include <Map.h>
#include <storage/Activity.h>

namespace ST {

struct DisplayedActivity : public std::enable_shared_from_this<DisplayedActivity> {
    struct RecordRange {
        size_t min;
        size_t max;
    };

    Activity                   activity;
    std::optional<size_t>      mark { };
    std::optional<RecordRange> segment;

    explicit DisplayedActivity(Activity const &activity);
    void clear_segment();
    void clear_mark();
    void set_mark(size_t t);
    void set_segment(size_t t);
};

struct ActivityDataDisplay : public Widget {
    std::shared_ptr<DisplayedActivity> activity;
    Texture2D                          sport_icon;

    explicit ActivityDataDisplay(pWidget const &parent);
    ~ActivityDataDisplay();
    void initialize() override;
    void draw() override;
    void resize() override;
    void process_input() override;
    void set_activity(std::shared_ptr<DisplayedActivity> const &activity);
};

struct ActivityMap : public Widget {
    std::shared_ptr<DisplayedActivity> activity;
    Map                                map;
    std::vector<Vector2>               track { };
    Box                                box;
    Image                              image;
    Texture2D                          texture;
    pTileDownload                      download { };                 // the tiles that weren't cached, while they download
    bool                               reporting_failures { false }; // the status line says tiles failed

    explicit ActivityMap(pWidget const &parent);
    ~ActivityMap();
    void initialize() override;
    void draw() override;
    void resize() override;
    void process_input() override;
    void set_activity(std::shared_ptr<DisplayedActivity> const &activity);
    void add_tile(MapTile const &tile);
    void tiles_done(MapTilesDone const &done);

private:
    void draw_tile(size_t index, std::string const &png);
    void stop_download();
};

struct ActivityGraph : public Widget {
    std::shared_ptr<DisplayedActivity> activity;
    bool                               dragging { false };
    Image                              image;
    Texture2D                          texture;

    explicit ActivityGraph(pWidget const &parent);
    ~ActivityGraph();
    void initialize() override;
    void draw() override;
    void resize() override;
    void process_input() override;
    void set_activity(std::shared_ptr<DisplayedActivity> const &activity);
};

struct ActivityDisplay : public Layout {
    std::shared_ptr<DisplayedActivity>   activity;
    std::shared_ptr<ActivityDataDisplay> data_display;
    std::shared_ptr<ActivityMap>         map;
    std::shared_ptr<ActivityGraph>       graph;

    explicit ActivityDisplay(pWidget const &parent);
    void set_activity(Activity const &activity);
    void clear_activity();
    void initialize() override;
};

}
