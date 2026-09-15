#pragma once

#include "raylib.h"
#include <Logging.h>
#include <Widget.h>
#include <storage/Activity.h>

namespace ST {

struct ActivityDataDisplay : public Widget {
    Texture2D sport_icon;

    explicit ActivityDataDisplay(pWidget const &parent);
    ~ActivityDataDisplay();
    void initialize() override;
    void draw() override;
    void resize() override;
    void process_input() override;
};

struct ActivityGraph : public Widget {
    struct RecordRange {
        size_t min;
        size_t max;
    };

    std::optional<Activity>    activity { };
    std::optional<size_t>      mark { };
    std::optional<RecordRange> segment;
    bool                       dragging { false };
    Image                      image;
    Texture2D                  texture;

    explicit ActivityGraph(pWidget const &parent);
    ~ActivityGraph();
    void initialize() override;
    void draw() override;
    void resize() override;
    void process_input() override;

    void set_activity(Activity const &activity);
    void clear_segment();
    void clear_mark();
    void set_mark(size_t t);
    void set_segment(size_t t);
};

struct ActivityDisplay : public Layout {
    std::optional<Activity>              activity { };
    std::shared_ptr<ActivityDataDisplay> data_display;
    std::shared_ptr<ActivityGraph>       graph;

    explicit ActivityDisplay(pWidget const &parent);
    void set_activity(Activity const &activity);
    void initialize() override;
};
}
