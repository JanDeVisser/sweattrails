/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests selecting laps in the activity on display, and the laps' parts of
 * the strip under the graph.
 */

#include <optional>
#include <vector>

#include <storage/Activity.h>
#include <storage/Storage.h>
#include <widget/DisplayedActivity.h>

#include <TestApp.h>

using namespace ST;

namespace {

// 100 records in laps of 10, 30 and 60 records.
struct TestActivity {
    Storage                            storage { };
    std::shared_ptr<DisplayedActivity> displayed;

    explicit TestActivity(std::vector<size_t> const &lap_ends = { 10, 40, 100 })
    {
        Activity activity { storage, ActivityID { } };
        activity.records.resize(lap_ends.empty() ? 0 : lap_ends.back());
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
    d.set_segment(12);
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

// A drag in the graph starts with clear_segment().
void test_drag_ends_the_lap()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.select_lap(2);
    d.clear_segment();
    d.set_segment(5);
    CHECK_EQ(lap(d.lap), -1);
    CHECK(d.segment.has_value() && d.segment->min == 5);
}

void test_toggle_lap()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.toggle_lap(0);
    CHECK_EQ(lap(d.lap), 0);
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
    CHECK_EQ(lap(d.lap), 0);
    d.next_lap();
    d.next_lap();
    CHECK_EQ(lap(d.lap), 2);
    d.next_lap(); // Stays on the last
    CHECK_EQ(lap(d.lap), 2);
    d.previous_lap();
    CHECK_EQ(lap(d.lap), 1);
    d.previous_lap();
    d.previous_lap(); // Stays on the first
    CHECK_EQ(lap(d.lap), 0);

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
    d.set_segment(50);
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

int main()
{
    test_current_record();
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
    return ST::Test::test_result();
}
