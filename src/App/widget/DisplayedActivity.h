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
#include <storage/Boundaries.h>
#include <storage/Comparison.h>
#include <widget/GraphView.h>

namespace ST {

// A range dragged in the graph that's shorter than this, such as a plain
// click, isn't a selection: it's cleared when the drag ends.
constexpr u32 MinRangeSeconds = 10;

struct DisplayedActivity : public std::enable_shared_from_this<DisplayedActivity> {
    struct RecordRange {
        size_t min;
        size_t max;

        bool operator==(RecordRange const &) const = default;
    };

    // While comparing (specs/33-comparing-recordings.md): the other
    // recording of the activity, and its records paired with this one's,
    // by pair_records().
    struct ComparedRecording {
        Activity                           activity;
        std::vector<std::optional<size_t>> pairs { };
    };

    Activity                   activity;
    std::optional<size_t>      mark { };
    std::optional<RecordRange> segment;
    std::optional<size_t>      lap { }; // The selected lap, whose records `segment` is
    // A dragged range's stats, for the data panel, once the drag has ended.
    std::optional<Segment> range_stats { };
    // Where a drag in the graph started, while it goes on.
    std::optional<size_t>            anchor { };
    std::optional<ComparedRecording> compared { };
    // The records the graph is zoomed to (specs/39-graph-zoom-and-pace.md),
    // apart from the selection: changing that leaves the zoom.
    std::optional<RecordRange> zoom { };

    explicit DisplayedActivity(Activity const &activity);
    void clear_segment();
    // A drag in the graph starts at record `t`: it ends a lap's selection,
    // and the range runs from `t` to wherever set_segment() takes it.
    void start_segment(size_t t);
    // The segment's first and last record, for an overlay's range.
    [[nodiscard]] std::optional<std::pair<size_t, size_t>> selection() const;
    void                                                   clear_mark();
    void                                                   set_mark(size_t t);
    // The drag is at record `t`: the range runs from where it started to
    // `t`, on either side. Nothing if no drag is going on: Esc, selecting
    // a lap or another activity end it.
    void set_segment(size_t t);
    // The drag is over: a range under MinRangeSeconds is cleared, and a
    // longer one gets its stats.
    void end_segment();

    // The record the overlay preview shows: the marked one, or else where a
    // drag in the graph is, the end of the range it's selecting, or else
    // the first.
    [[nodiscard]] size_t current_record() const;

    // Whether there are laps to select: one lap with records is the whole
    // activity.
    [[nodiscard]] bool has_laps() const;
    // The lap that record `t` is in.
    [[nodiscard]] std::optional<size_t> lap_of(size_t t) const;
    void                                select_lap(size_t ix);
    // After a switch from another recording of the activity: the lap that
    // was selected there, by its number, if this one has it.
    void keep_lap(std::optional<size_t> ix);
    // Selects the lap, or goes back to the whole activity if it's selected.
    void toggle_lap(size_t ix);
    // The next lap, or the first if none is selected. Stays on the last.
    void next_lap();
    // The previous lap, or the last if none is selected. Stays on the first.
    void previous_lap();
    void whole_activity();

    // Moves the selection's start back, or its end on, to the next km, lap
    // or stop (specs/38-extend-selection.md). It extends a dragged range
    // or a lap, or else starts from `from`, the record right-clicked, or
    // the mark, the record last hovered. The new range is no lap's, and has its stats at once,
    // however short: it was chosen, not dragged by accident. False if
    // there's nothing to start from; with nothing to go to, it stays as
    // it was.
    bool extend(Boundary boundary, Direction direction, std::optional<size_t> from = { });
    // Whether extend() would change the selection.
    [[nodiscard]] bool can_extend(Boundary boundary, Direction direction, std::optional<size_t> from = { }) const;

    // Zooms the graph to the selection, also when zoomed in already. A
    // selection of one record, or the one zoomed to, isn't something to
    // zoom to.
    void               zoom_in();
    [[nodiscard]] bool can_zoom_in() const;
    // Back to the whole activity, but not while a drag is going on: the
    // range being dragged is in the zoomed view.
    void               zoom_out();
    [[nodiscard]] bool can_zoom_out() const;
    // Zooms out when zoomed in, and in otherwise, as Z does.
    void toggle_zoom();
    // The records the graph shows: the zoom, or the whole activity.
    [[nodiscard]] GraphView view() const;

    // Compares the activity with `other`, another recording of it, or stops.
    void compare_with(std::optional<Activity> const &other);
    // The comparison of what's selected: the lap, the dragged range once
    // the drag has ended, or else the whole activity. None while not
    // comparing.
    [[nodiscard]] std::optional<Comparison> comparison() const;
    // The comparison of lap `ix`, for the lap table.
    [[nodiscard]] std::optional<Comparison> lap_comparison(size_t ix) const;

private:
    // What extend() starts from: the selection, or else `from` or the mark.
    [[nodiscard]] std::optional<RecordRange> extension_base(std::optional<size_t> from) const;
    [[nodiscard]] std::optional<size_t>      extension_target(Boundary boundary, Direction direction, std::optional<size_t> from) const;
    [[nodiscard]] std::optional<Comparison>  comparison_of(size_t first, size_t end) const;

    // The last comparison_of(): the data panel asks for it every frame.
    struct CachedComparison {
        size_t     first;
        size_t     end;
        Comparison comparison;
    };
    mutable std::optional<CachedComparison> cached { };
};

// A lap's part of the strip under the graph, from x0 up to x1. The graph's
// x-axis is the record, so a lap is as wide as its share of the records.
struct LapSpan {
    float x0;
    float x1;
};

// The laps' parts of a strip `width` wide under a graph showing `view`, a
// span per lap. A lap partly outside the view is cut off at its edge, and
// one wholly outside it is an empty span there.
std::vector<LapSpan> lap_spans(Activity const &activity, GraphView const &view, float width);

// The lap whose part of the strip `x` is in, if any.
std::optional<size_t> lap_at(std::vector<LapSpan> const &spans, float x);

}
