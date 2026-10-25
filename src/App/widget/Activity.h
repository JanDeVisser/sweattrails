#pragma once

#include <memory>
#include <ranges>

#include <raylib.h>

#include <Logging.h>

#include <Widget.h>

#include <Map.h>
#include <storage/Activity.h>
#include <widget/DataFields.h>
#include <widget/DisplayedActivity.h>

namespace ST {

struct ActivityDataDisplay : public Widget {
    // A field that marks a record when it's clicked, where the last draw()
    // drew it, in screen coordinates.
    struct ClickableField {
        Rectangle area;
        size_t    mark_at;
    };

    std::shared_ptr<DisplayedActivity> activity;
    Texture2D                          sport_icon;
    std::vector<ClickableField>        clickable { };

    explicit ActivityDataDisplay(pWidget const &parent);
    ~ActivityDataDisplay();
    void initialize() override;
    void draw() override;
    void resize() override;
    void process_input() override;
    void set_activity(std::shared_ptr<DisplayedActivity> const &activity);

private:
    void draw_fields(float y);
    void draw_field(DataField const &field, float x, float value_x, float y, float height);
};

struct ActivityMap : public Widget {
    std::shared_ptr<DisplayedActivity> activity;
    Map                                map;
    std::vector<Vector2>               track { };
    Box                                box;
    Image                              image { };                    // empty while no map is shown
    Texture2D                          texture { };                  // empty while no map is shown
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
    Image                              image { };   // empty while no graph is shown
    Texture2D                          texture { }; // empty while no graph is shown

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
