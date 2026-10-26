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
    if (segment) {
        if (t < segment->min) {
            segment->min = t;
            segment->max = t;
        } else {
            segment->max = t;
        }
    } else {
        segment = RecordRange { .min = t, .max = t };
    }
}

size_t DisplayedActivity::current_record() const
{
    if (mark) {
        return *mark;
    }
    if (segment) {
        return segment->max;
    }
    return 0;
}

bool DisplayedActivity::has_laps() const
{
    return activity.laps.size() > 1;
}

void DisplayedActivity::select_lap(size_t ix)
{
    if (ix >= activity.laps.size()) {
        return;
    }
    auto const &l = activity.laps[ix];
    lap = ix;
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
