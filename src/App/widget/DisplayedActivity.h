/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The activity on display, and what's picked out in it: a marked record, a
 * range of records dragged in the graph, or a lap. The widgets that show
 * the activity share one.
 */

#pragma once

#include <memory>
#include <optional>
#include <vector>

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
    std::optional<size_t>      lap { }; // The selected lap, whose records `segment` is

    explicit DisplayedActivity(Activity const &activity);
    void clear_segment();
    void clear_mark();
    void set_mark(size_t t);
    void set_segment(size_t t);

    // The record the overlay preview shows: the marked one, or else where a
    // drag in the graph is, the end of the range it's selecting, or else
    // the first.
    [[nodiscard]] size_t current_record() const;

    // Whether there are laps to select: one lap is the whole activity.
    [[nodiscard]] bool has_laps() const;
    void               select_lap(size_t ix);
    // Selects the lap, or goes back to the whole activity if it's selected.
    void toggle_lap(size_t ix);
    // The next lap, or the first if none is selected. Stays on the last.
    void next_lap();
    // The previous lap, or the last if none is selected. Stays on the first.
    void previous_lap();
    void whole_activity();
};

// A lap's part of the strip under the graph, from x0 up to x1. The graph's
// x-axis is the record, so a lap is as wide as its share of the records.
struct LapSpan {
    float x0;
    float x1;
};

// The laps' parts of a strip `width` wide, a span per lap.
std::vector<LapSpan> lap_spans(Activity const &activity, float width);

// The lap whose part of the strip `x` is in, if any.
std::optional<size_t> lap_at(std::vector<LapSpan> const &spans, float x);

}
