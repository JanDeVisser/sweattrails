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
    return laps_with_records(activity) > 1;
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

void DisplayedActivity::keep_lap(std::optional<size_t> ix)
{
    if (ix && has_laps()) {
        select_lap(*ix);
    }
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

bool DisplayedActivity::can_zoom_in() const
{
    return segment && !anchor && segment->max > segment->min && segment->max < activity.records.size() && segment != zoom;
}

void DisplayedActivity::zoom_in()
{
    if (can_zoom_in()) {
        zoom = segment;
    }
}

bool DisplayedActivity::can_zoom_out() const
{
    return zoom && !anchor;
}

void DisplayedActivity::zoom_out()
{
    if (can_zoom_out()) {
        zoom.reset();
    }
}

void DisplayedActivity::toggle_zoom()
{
    if (zoom) {
        zoom_out();
    } else {
        zoom_in();
    }
}

GraphView DisplayedActivity::view() const
{
    if (zoom && zoom->max < activity.records.size()) {
        return GraphView { .first = zoom->min, .end = zoom->max + 1 };
    }
    return GraphView { .first = 0, .end = activity.records.size() };
}

std::optional<DisplayedActivity::RecordRange> DisplayedActivity::extension_base(std::optional<size_t> from) const
{
    // Not while a drag is going on: the range isn't there yet.
    if (segment && !anchor) {
        return segment;
    }
    if (!from) {
        from = mark;
    }
    if (from && *from < activity.records.size()) {
        return RecordRange { .min = *from, .max = *from };
    }
    return { };
}

std::optional<size_t> DisplayedActivity::extension_target(Boundary boundary, Direction direction, std::optional<size_t> from) const
{
    auto const base = extension_base(from);
    if (!base) {
        return { };
    }
    return extend_to(activity, direction == Direction::Back ? base->min : base->max, boundary, direction);
}

bool DisplayedActivity::can_extend(Boundary boundary, Direction direction, std::optional<size_t> from) const
{
    return extension_target(boundary, direction, from).has_value();
}

bool DisplayedActivity::extend(Boundary boundary, Direction direction, std::optional<size_t> from)
{
    auto base = extension_base(from);
    if (!base) {
        return false;
    }
    auto const target = extension_target(boundary, direction, from);
    if (!target) {
        return true;
    }
    (direction == Direction::Back ? base->min : base->max) = *target;
    lap.reset();
    anchor.reset();
    segment = base;
    range_stats = range_segment(activity, base->min, base->max);
    return true;
}

std::vector<LapSpan> lap_spans(Activity const &activity, GraphView const &view, float width)
{
    std::vector<LapSpan> spans;
    if (view.count() == 0) {
        return spans;
    }
    auto const clip = [&view](size_t ix) { return std::clamp(ix, view.first, view.end); };
    for (auto const &lap : activity.laps) {
        spans.push_back({ .x0 = view_x(view, clip(lap.first_record), width), .x1 = view_x(view, clip(lap.end_record), width) });
    }
    return spans;
}

std::optional<size_t> lap_at(std::vector<LapSpan> const &spans, float x)
{
    for (auto ix = 0uz; ix < spans.size(); ++ix) {
        // A lap outside the view, or without records, has nothing to click.
        if (spans[ix].x1 <= spans[ix].x0) {
            continue;
        }
        auto const last = ix + 1 == spans.size();
        if (x >= spans[ix].x0 && (x < spans[ix].x1 || (last && x <= spans[ix].x1))) {
            return ix;
        }
    }
    return { };
}

void DisplayedActivity::compare_with(std::optional<Activity> const &other)
{
    cached.reset();
    if (!other) {
        compared.reset();
        return;
    }
    compared = ComparedRecording { .activity = *other, .pairs = pair_records(activity.recorded, other->recorded) };
}

void DisplayedActivity::set_route(std::optional<Activity> const &other)
{
    if (!other || other->records.empty()) {
        route.reset();
        return;
    }
    route = RouteRecording { .activity = *other, .nearest = nearest_records(activity.records, other->records) };
}

size_t DisplayedActivity::route_index(size_t t) const
{
    if (!route) {
        return t;
    }
    return (t < route->nearest.size()) ? route->nearest[t] : route->activity.records.size() - 1;
}

std::optional<size_t> route_source(std::span<Activity const> recordings)
{
    std::optional<size_t> ret;
    for (size_t ix = 0; ix < recordings.size(); ++ix) {
        auto const &recording = recordings[ix];
        if (!recording.segment.box || recording.records.empty()) {
            continue;
        }
        if (!ret || recording.segment.elapsed.elapsed > recordings[*ret].segment.elapsed.elapsed) {
            ret = ix;
        }
    }
    return ret;
}

std::vector<size_t> nearest_records(std::span<FIT::record const> shown, std::span<FIT::record const> route)
{
    std::vector<size_t> ret;
    ret.reserve(shown.size());
    auto const time = [](FIT::record const &rec) { return static_cast<i64>(rec.timestamp.timestamp); };
    for (auto const &rec : shown) {
        auto const t = time(rec);
        auto const after = std::ranges::lower_bound(route, t, { }, time);
        auto       ix = static_cast<size_t>(after - route.begin());
        if (ix == route.size() || (ix > 0 && t - time(route[ix - 1]) <= time(route[ix]) - t)) {
            --ix;
        }
        ret.push_back(ix);
    }
    return ret;
}

std::optional<Comparison> DisplayedActivity::comparison_of(size_t first, size_t end) const
{
    if (!compared) {
        return { };
    }
    if (!cached || cached->first != first || cached->end != end) {
        cached = CachedComparison { first, end, compare(activity.recorded, compared->activity.recorded, compared->pairs, first, end) };
    }
    return cached->comparison;
}

std::optional<Comparison> DisplayedActivity::comparison() const
{
    if (lap && *lap < activity.laps.size()) {
        return lap_comparison(*lap);
    }
    if (segment && range_stats) {
        return comparison_of(segment->min, segment->max + 1);
    }
    return whole_comparison();
}

std::optional<Comparison> DisplayedActivity::whole_comparison() const
{
    return comparison_of(0, activity.recorded.size());
}

std::optional<Comparison> DisplayedActivity::lap_comparison(size_t ix) const
{
    if (ix >= activity.laps.size()) {
        return { };
    }
    return comparison_of(activity.laps[ix].first_record, activity.laps[ix].end_record);
}

}
