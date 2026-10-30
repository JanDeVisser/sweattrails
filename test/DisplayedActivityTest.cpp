/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests selecting laps and ranges in the activity on display, and the
 * laps' parts of the strip under the graph.
 */

#include <optional>
#include <utility>
#include <vector>

#include <storage/Activity.h>
#include <storage/Storage.h>
#include <widget/DisplayedActivity.h>

#include <TestApp.h>

using namespace ST;

namespace {

// 100 records, a second apart, in laps of 10, 30 and 60 records.
struct TestActivity {
    Storage                            storage { };
    std::shared_ptr<DisplayedActivity> displayed;

    explicit TestActivity(std::vector<size_t> const &lap_ends = { 10, 40, 100 })
    {
        Activity activity { storage, ActivityID { } };
        activity.records.resize(lap_ends.empty() ? 0 : lap_ends.back());
        for (size_t ix = 0; ix < activity.records.size(); ++ix) {
            activity.records[ix].timestamp.timestamp = 1'000'000 + static_cast<u32>(ix);
        }
        activity.recorded = activity.records;
        size_t first = 0;
        for (auto end : lap_ends) {
            activity.laps.push_back(Lap { .first_record = first, .end_record = end });
            first = end;
        }
        displayed = std::make_shared<DisplayedActivity>(activity);
    }
};

// A lap as an int, -1 for none, for CHECK_EQ: there's no formatter for
// std::optional.
int lap(std::optional<size_t> lap)
{
    return lap ? static_cast<int>(*lap) : -1;
}

// A drag in the graph clears the mark, and selects a range up to where the
// mouse is: the preview follows that, rather than jumping to the start.
void test_current_record()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.clear_segment();
    CHECK_EQ(d.current_record(), 0uz);
    d.set_mark(17);
    CHECK_EQ(d.current_record(), 17uz);
    // A drag from 12 to 25, and back to 5.
    d.clear_mark();
    d.start_segment(12);
    d.set_segment(25);
    CHECK_EQ(d.current_record(), 25uz);
    d.set_segment(5);
    CHECK_EQ(d.current_record(), 5uz);
    // Let go: the mark wins.
    d.set_mark(30);
    CHECK_EQ(d.current_record(), 30uz);
}

void test_select_lap()
{
    TestActivity t;
    auto        &d = *t.displayed;
    CHECK(d.has_laps());
    d.select_lap(1);
    CHECK_EQ(lap(d.lap), 1);
    CHECK(d.segment.has_value());
    if (d.segment) {
        CHECK_EQ(d.segment->min, 10uz);
        CHECK_EQ(d.segment->max, 39uz);
    }
    // There's no fourth lap.
    d.select_lap(3);
    CHECK_EQ(lap(d.lap), 1);
}

// A drag in the graph starts with start_segment(), which clears the lap.
void test_drag_ends_the_lap()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.select_lap(2);
    d.start_segment(5);
    CHECK_EQ(lap(d.lap), -1);
    CHECK(d.segment.has_value() && d.segment->min == 5);
}

void test_toggle_lap()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.toggle_lap(0);
    CHECK(d.lap == 0uz);
    d.toggle_lap(2);
    CHECK_EQ(lap(d.lap), 2);
    d.toggle_lap(2);
    CHECK_EQ(lap(d.lap), -1);
    CHECK(!d.segment);
}

void test_next_and_previous()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.next_lap();
    CHECK(d.lap == 0uz);
    d.next_lap();
    d.next_lap();
    CHECK_EQ(lap(d.lap), 2);
    d.next_lap(); // Stays on the last
    CHECK_EQ(lap(d.lap), 2);
    d.previous_lap();
    CHECK_EQ(lap(d.lap), 1);
    d.previous_lap();
    d.previous_lap(); // Stays on the first
    CHECK(d.lap == 0uz);

    d.whole_activity();
    CHECK_EQ(lap(d.lap), -1);
    CHECK(!d.segment);
    d.previous_lap();
    CHECK_EQ(lap(d.lap), 2);
}

void test_one_lap()
{
    TestActivity t { { 50 } };
    auto        &d = *t.displayed;
    CHECK(!d.has_laps());
    d.next_lap();
    d.previous_lap();
    CHECK_EQ(lap(d.lap), -1);
    CHECK(!d.segment);
}

// A lap without records selects, but has no range to show.
void test_empty_lap()
{
    TestActivity t { { 10, 10, 20 } };
    auto        &d = *t.displayed;
    d.select_lap(1);
    CHECK_EQ(lap(d.lap), 1);
    CHECK(!d.segment);
}

// Down and Up go through a lap without records like any other.
void test_next_over_an_empty_lap()
{
    TestActivity t { { 10, 10, 20 } };
    auto        &d = *t.displayed;
    d.next_lap();
    d.next_lap();
    CHECK_EQ(lap(d.lap), 1);
    CHECK(!d.segment);
    d.next_lap();
    CHECK_EQ(lap(d.lap), 2);
    CHECK(d.segment.has_value() && d.segment->min == 10 && d.segment->max == 19);
    d.toggle_lap(1);
    CHECK_EQ(lap(d.lap), 1);
    d.toggle_lap(1);
    CHECK_EQ(lap(d.lap), -1);
}

// A lap without records has no width in the strip, and can't be clicked:
// the boundary is the next lap's.
void test_empty_lap_in_the_strip()
{
    TestActivity t { { 10, 10, 20 } };
    auto const   spans = lap_spans(t.displayed->activity, 200);
    CHECK_EQ(spans.size(), 3uz);
    if (spans.size() == 3) {
        CHECK_EQ(spans[1].x0, spans[1].x1);
    }
    CHECK_EQ(lap(lap_at(spans, 100)), 2);
    CHECK_EQ(lap(lap_at(spans, 99.9f)), 0);
}

// A lap selected with the keys replaces a range dragged in the graph.
void test_select_lap_replaces_a_dragged_range()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.start_segment(50);
    d.set_segment(70);
    d.next_lap();
    CHECK(d.segment.has_value() && d.segment->min == 0 && d.segment->max == 9);
}

void test_lap_spans()
{
    TestActivity t;
    auto const   spans = lap_spans(t.displayed->activity, 500);
    CHECK_EQ(spans.size(), 3uz);
    if (spans.size() == 3) {
        CHECK_EQ(spans[0].x0, 0.0f);
        CHECK_EQ(spans[0].x1, 50.0f);
        CHECK_EQ(spans[1].x0, 50.0f);
        CHECK_EQ(spans[1].x1, 200.0f);
        CHECK_EQ(spans[2].x1, 500.0f);
    }
    TestActivity no_records { { } };
    CHECK(lap_spans(no_records.displayed->activity, 500).empty());
}

void test_lap_at()
{
    TestActivity t;
    auto const   spans = lap_spans(t.displayed->activity, 500);
    CHECK_EQ(lap(lap_at(spans, 0)), 0);
    CHECK_EQ(lap(lap_at(spans, 49.9f)), 0);
    CHECK_EQ(lap(lap_at(spans, 50)), 1);
    CHECK_EQ(lap(lap_at(spans, 500)), 2);
    CHECK_EQ(lap(lap_at(spans, 500.5f)), -1);
    CHECK_EQ(lap(lap_at(spans, -1)), -1);
}

}

// The range an overlay starts from: none, a dragged range, or a lap's
// records, from its first to its last.
void test_selection()
{
    TestActivity t;
    auto        &d = *t.displayed;
    CHECK(!d.selection());
    d.start_segment(12);
    d.set_segment(25);
    CHECK(d.selection() == (std::pair { 12uz, 25uz }));
    d.select_lap(1);
    CHECK(d.selection() == (std::pair { 10uz, 39uz }));
    d.whole_activity();
    CHECK(!d.selection());
}

// A drag selects from where it started to where the mouse is, either
// side, and back across where it started.
void test_drag_either_way()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.start_segment(50);
    d.set_segment(30);
    CHECK(d.segment.has_value() && d.segment->min == 30 && d.segment->max == 50);
    CHECK_EQ(d.current_record(), 30uz);
    d.set_segment(20);
    CHECK(d.segment.has_value() && d.segment->min == 20 && d.segment->max == 50);
    d.set_segment(70);
    CHECK(d.segment.has_value() && d.segment->min == 50 && d.segment->max == 70);
    CHECK_EQ(d.current_record(), 70uz);
    d.end_segment();
    CHECK(d.segment.has_value() && d.segment->min == 50 && d.segment->max == 70);
    // The next drag starts afresh.
    d.start_segment(10);
    d.set_segment(5);
    CHECK(d.segment.has_value() && d.segment->min == 5 && d.segment->max == 10);
    CHECK(!d.range_stats.has_value());
}

// Let go, a range of 10 s or more gets its stats; a shorter one, such as a
// plain click, is cleared.
void test_end_segment()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.start_segment(40);
    d.set_segment(30);
    d.end_segment();
    CHECK(d.segment.has_value());
    CHECK(d.range_stats.has_value());
    CHECK(d.range_stats.has_value() && d.range_stats->elapsed.elapsed == 10u);
    CHECK(!d.anchor.has_value());

    d.start_segment(40);
    d.set_segment(31);
    d.end_segment();
    CHECK(!d.segment.has_value());
    CHECK(!d.range_stats.has_value());

    d.start_segment(40);
    d.end_segment();
    CHECK(!d.segment.has_value());
    CHECK_EQ(d.current_record(), 0uz);
}

// A new drag, Esc and selecting a lap clear a range's stats.
void test_range_stats_cleared()
{
    TestActivity t;
    auto        &d = *t.displayed;
    auto const   drag = [&d] {
        d.start_segment(20);
        d.set_segment(60);
        d.end_segment();
        CHECK(d.range_stats.has_value());
    };
    drag();
    d.start_segment(5);
    CHECK(!d.range_stats.has_value());
    drag();
    d.whole_activity();
    CHECK(!d.range_stats.has_value());
    CHECK(!d.segment.has_value());
    drag();
    d.next_lap();
    CHECK(!d.range_stats.has_value());
    CHECK(d.lap == 0uz);
}

// Up or Down while the button is held selects a lap mid-drag. The graph
// goes on calling set_segment() until the button is let go, so the drag
// mustn't leave a lap selected with a range that isn't the lap's: the
// panel would show the lap's stats, and the map and graph another range.
void test_lap_selected_during_a_drag()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.start_segment(20);
    d.set_segment(60);
    d.next_lap(); // Down, with the button held
    d.set_segment(65);
    d.set_segment(80);
    d.end_segment();
    if (d.lap) {
        CHECK(d.segment.has_value() && d.segment->min == 0 && d.segment->max == 9);
    } else {
        CHECK(!d.segment.has_value() || d.range_stats.has_value());
    }
}

// Esc during a drag ends it: the mouse moving on, and letting go, select
// nothing.
void test_esc_during_a_drag()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.start_segment(20);
    d.set_segment(60);
    d.whole_activity();
    d.set_segment(70);
    d.end_segment();
    CHECK(!d.segment.has_value());
    CHECK(!d.range_stats.has_value());
}

// A watch can correct its clock a few seconds back. A range whose last
// record has an earlier time than its first is under 10 s, not 136 years.
void test_range_with_time_going_back()
{
    TestActivity t;
    auto        &d = *t.displayed;
    for (auto *records : { &d.activity.records, &d.activity.recorded }) {
        (*records)[55].timestamp.timestamp = 1'000'045;
    }
    d.start_segment(50);
    d.set_segment(55);
    d.end_segment();
    CHECK(!d.range_stats.has_value() || d.range_stats->elapsed.elapsed < 3600u);
}

int main()
{
    test_current_record();
    test_drag_either_way();
    test_end_segment();
    test_range_stats_cleared();
    test_lap_selected_during_a_drag();
    test_esc_during_a_drag();
    test_select_lap();
    test_drag_ends_the_lap();
    test_toggle_lap();
    test_next_and_previous();
    test_one_lap();
    test_empty_lap();
    test_next_over_an_empty_lap();
    test_empty_lap_in_the_strip();
    test_select_lap_replaces_a_dragged_range();
    test_lap_spans();
    test_lap_at();
    test_selection();
    return ST::Test::test_result();
}
