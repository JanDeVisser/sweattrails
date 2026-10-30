/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>

#include <widget/DisplayedActivity.h>

namespace ST {

DisplayedActivity::DisplayedActivity(Activity const &activity)
    : activity(activity)
{
}

// A drag in the graph starts with this, so it ends a lap's selection too.
void DisplayedActivity::clear_segment()
{
    segment.reset();
    lap.reset();
    range_stats.reset();
    anchor.reset();
}

void DisplayedActivity::start_segment(size_t t)
{
    clear_segment();
    anchor = t;
    set_segment(t);
}

std::optional<std::pair<size_t, size_t>> DisplayedActivity::selection() const
{
    if (!segment) {
        return { };
    }
    return std::pair { segment->min, segment->max };
}

void DisplayedActivity::clear_mark()
{
    mark.reset();
}

void DisplayedActivity::set_mark(size_t t)
{
    mark = t;
}

void DisplayedActivity::set_segment(size_t t)
{
    // Esc, a lap, or another activity during the drag ended it.
    if (!anchor) {
        return;
    }
    segment = RecordRange { .min = std::min(*anchor, t), .max = std::max(*anchor, t) };
}

void DisplayedActivity::end_segment()
{
    anchor.reset();
    if (!segment || lap) {
        return;
    }
    auto const &records = activity.records;
    // Signed: a watch can set its clock back, and the last record can be
    // earlier than the first.
    auto const seconds = [&records](size_t ix) { return static_cast<i64>(records[ix].timestamp.timestamp); };
    if (segment->max >= records.size() || seconds(segment->max) - seconds(segment->min) < MinRangeSeconds) {
        segment.reset();
        return;
    }
    range_stats = range_segment(activity, segment->min, segment->max);
}

size_t DisplayedActivity::current_record() const
{
    if (mark) {
        return *mark;
    }
    // Where the drag is: the range's left end when it went left.
    if (segment) {
        return (anchor && segment->min < *anchor) ? segment->min : segment->max;
    }
    return 0;
}

bool DisplayedActivity::has_laps() const
{
    return activity.laps.size() > 1;
}

std::optional<size_t> DisplayedActivity::lap_of(size_t t) const
{
    for (size_t ix = 0; ix < activity.laps.size(); ++ix) {
        if (t >= activity.laps[ix].first_record && t < activity.laps[ix].end_record) {
            return ix;
        }
    }
    return { };
}

void DisplayedActivity::select_lap(size_t ix)
{
    if (ix >= activity.laps.size()) {
        return;
    }
    auto const &l = activity.laps[ix];
    lap = ix;
    range_stats.reset();
    anchor.reset();
    // A lap without records, which a watch can log, has no range to show.
    if (l.end_record > l.first_record && l.end_record <= activity.records.size()) {
        segment = RecordRange { .min = l.first_record, .max = l.end_record - 1 };
    } else {
        segment.reset();
    }
}

void DisplayedActivity::toggle_lap(size_t ix)
{
    if (lap == ix) {
        whole_activity();
    } else {
        select_lap(ix);
    }
}

void DisplayedActivity::next_lap()
{
    if (!has_laps()) {
        return;
    }
    select_lap(lap ? std::min(*lap + 1, activity.laps.size() - 1) : 0);
}

void DisplayedActivity::previous_lap()
{
    if (!has_laps()) {
        return;
    }
    select_lap(lap ? std::max(*lap, 1uz) - 1 : activity.laps.size() - 1);
}

void DisplayedActivity::whole_activity()
{
    clear_segment();
}

std::vector<LapSpan> lap_spans(Activity const &activity, float width)
{
    std::vector<LapSpan> spans;
    if (activity.records.empty()) {
        return spans;
    }
    auto const dt = width / static_cast<float>(activity.records.size());
    for (auto const &lap : activity.laps) {
        spans.push_back({ .x0 = static_cast<float>(lap.first_record) * dt, .x1 = static_cast<float>(lap.end_record) * dt });
    }
    return spans;
}

std::optional<size_t> lap_at(std::vector<LapSpan> const &spans, float x)
{
    for (auto ix = 0uz; ix < spans.size(); ++ix) {
        auto const last = ix + 1 == spans.size();
        if (x >= spans[ix].x0 && (x < spans[ix].x1 || (last && x <= spans[ix].x1))) {
            return ix;
        }
    }
    return { };
}

}
