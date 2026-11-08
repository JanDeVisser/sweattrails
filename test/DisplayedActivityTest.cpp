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

// Tab to another recording, with lap 2 selected on the one before: it's
// kept if this one has a lap 2.
void test_keep_lap()
{
    TestActivity t;
    t.displayed->keep_lap(2);
    CHECK_EQ(lap(t.displayed->lap), 2);
    CHECK(t.displayed->segment.has_value() && t.displayed->segment->min == 40);

    TestActivity fewer { { 10, 100 } };
    fewer.displayed->keep_lap(2);
    CHECK_EQ(lap(fewer.displayed->lap), -1);

    TestActivity none { { 100 } };
    none.displayed->keep_lap(0);
    CHECK_EQ(lap(none.displayed->lap), -1);

    t.displayed->whole_activity();
    t.displayed->keep_lap({ });
    CHECK_EQ(lap(t.displayed->lap), -1);
}

// Comparing with another recording: the whole activity, a lap, and a
// dragged range, each over its own records.
void test_comparison()
{
    TestActivity t;
    auto        &d = *t.displayed;
    CHECK(!d.comparison().has_value());
    for (auto ix = 0uz; ix < d.activity.recorded.size(); ++ix) {
        d.activity.recorded[ix].power = static_cast<u16>(ix < 10 ? 100 : 200);
    }
    auto other = d.activity;
    for (auto &rec : other.recorded) {
        rec.power = static_cast<u16>(*rec.power - 10);
    }
    d.compare_with(other);
    auto const whole = d.comparison();
    CHECK(whole.has_value() && whole->avg_power.shown == 190.0 && whole->avg_power.other == 180.0);

    d.select_lap(0);
    auto const lap = d.comparison();
    CHECK(lap.has_value() && lap->avg_power.shown == 100.0 && lap->avg_power.other == 90.0);
    CHECK(d.lap_comparison(1).has_value() && d.lap_comparison(1)->avg_power.shown == 200.0);
    CHECK(!d.lap_comparison(3).has_value());

    // A range of records 5 to 24, once the drag has ended: 5 at 100 W
    // and 15 at 200 W.
    d.whole_activity();
    d.start_segment(5);
    d.set_segment(24);
    d.end_segment();
    auto const range = d.comparison();
    CHECK(range.has_value() && range->avg_power.shown == 175.0 && range->avg_power.other == 165.0);

    // Another one to compare with: the comparison isn't the one before's.
    d.whole_activity();
    CHECK(d.comparison()->avg_power.other == 180.0);
    for (auto &rec : other.recorded) {
        rec.power = static_cast<u16>(*rec.power - 10);
    }
    d.compare_with(other);
    CHECK(d.comparison()->avg_power.other == 170.0);

    d.compare_with(std::nullopt);
    CHECK(!d.comparison().has_value());
}

// Reviewer: the other recording started 20 s late, at a lower rate: a lap
// compares over the lap's clock times, so lap 0 (records 0 to 9) has
// nothing of the other's, and lap 1 (10 to 39) only its last 20 seconds.
// The pairs index `records` as well as `recorded`, for the graph.
void test_comparison_of_a_lap_by_the_clock()
{
    TestActivity t;
    auto        &d = *t.displayed;
    for (auto &rec : d.activity.recorded) {
        rec.power = 200;
    }
    Activity other { d.activity };
    other.records.clear();
    for (auto ix = 0uz; ix < 40; ++ix) {
        record rec { };
        rec.timestamp.timestamp = 1'000'020 + 2 * static_cast<u32>(ix); // Every 2 s from record 20's time
        rec.power = 190;
        other.records.push_back(rec);
    }
    other.recorded = other.records;
    d.compare_with(other);
    CHECK(d.compared.has_value());
    if (!d.compared) {
        return;
    }
    CHECK_EQ(d.compared->pairs.size(), d.activity.records.size());
    CHECK(!d.compared->pairs[19].has_value());
    CHECK(d.compared->pairs[20] == std::optional<size_t> { 0 });
    CHECK(d.compared->pairs[21] == std::optional<size_t> { 0 });
    CHECK(d.compared->pairs[22] == std::optional<size_t> { 1 });
    CHECK(d.compared->pairs[99] == std::optional<size_t> { 39 });

    auto const first = d.lap_comparison(0);
    CHECK(first.has_value() && first->avg_power.shown == 200.0 && !first->avg_power.other);
    auto const second = d.lap_comparison(1);
    CHECK(second.has_value() && second->avg_power.shown == 200.0 && second->avg_power.other == 190.0);

    // A new DisplayedActivity, as Tab and Left and Right make, compares
    // with nothing until compare_if_on() sets it again.
    DisplayedActivity const next { d.activity };
    CHECK(!next.compared.has_value() && !next.comparison().has_value());
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

// The lap a record is in, as a marked range's lap is selected by its first
// record.
void test_lap_of()
{
    TestActivity t;
    auto        &d = *t.displayed;
    CHECK(d.lap_of(0) == 0uz);
    CHECK(d.lap_of(9) == 0uz);
    CHECK(d.lap_of(10) == 1uz);
    CHECK(d.lap_of(99) == 2uz);
    CHECK(!d.lap_of(100).has_value());
}

// A dragged range from 15 to 25, in the second lap, extends to the laps'
// ends, and again to the next lap's.
void test_extend_a_range()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.start_segment(15);
    d.set_segment(25);
    d.end_segment();
    CHECK(d.extend(Boundary::Lap, Direction::On));
    CHECK(d.segment.has_value() && d.segment->min == 15 && d.segment->max == 39);
    CHECK(d.range_stats.has_value());
    CHECK(d.extend(Boundary::Lap, Direction::On));
    CHECK(d.segment.has_value() && d.segment->max == 99);
    CHECK(d.extend(Boundary::Lap, Direction::Back));
    CHECK(d.segment.has_value() && d.segment->min == 10 && d.segment->max == 99);
}

// A selected lap is a range once extended: no lap is selected.
void test_extend_a_lap()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.select_lap(1);
    CHECK(d.extend(Boundary::Lap, Direction::On));
    CHECK(!d.lap.has_value());
    CHECK(d.segment.has_value() && d.segment->min == 10 && d.segment->max == 99);
    CHECK(d.range_stats.has_value());
}

// Without a selection, from the mark: the record right-clicked. A range
// of a few seconds isn't cleared, as a short drag is.
void test_extend_from_the_mark()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.set_mark(36);
    CHECK(d.can_extend(Boundary::Lap, Direction::On));
    CHECK(d.extend(Boundary::Lap, Direction::On));
    CHECK(d.segment.has_value() && d.segment->min == 36 && d.segment->max == 39);
    CHECK(d.range_stats.has_value());
}

void test_extend_from_nothing()
{
    TestActivity t;
    auto        &d = *t.displayed;
    CHECK(!d.can_extend(Boundary::Lap, Direction::On));
    CHECK(!d.extend(Boundary::Lap, Direction::On));
    CHECK(!d.segment.has_value());
}

// Nothing to go to: no distances for a km, and nothing after the last
// lap. The selection stays as it was.
void test_extend_to_nothing()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.select_lap(2);
    CHECK(!d.can_extend(Boundary::Km, Direction::On));
    CHECK(!d.can_extend(Boundary::Lap, Direction::On));
    CHECK(d.extend(Boundary::Lap, Direction::On));
    CHECK(d.lap == 2uz);
    CHECK(d.segment.has_value() && d.segment->min == 40 && d.segment->max == 99);
}

// While a drag goes on there's no range to extend yet.
void test_no_extending_while_dragging()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.start_segment(15);
    d.set_segment(25);
    CHECK(!d.can_extend(Boundary::Lap, Direction::On));
}

// Reviewer: back from the mark, to the start of its lap, and on again.
void test_extend_back_from_the_mark()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.set_mark(25);
    CHECK(d.extend(Boundary::Lap, Direction::Back));
    CHECK(d.segment.has_value() && d.segment->min == 10 && d.segment->max == 25);
    CHECK(d.range_stats.has_value());
    CHECK(d.extend(Boundary::Lap, Direction::Back));
    CHECK(d.segment.has_value() && d.segment->min == 0 && d.segment->max == 25);
    CHECK(!d.can_extend(Boundary::Lap, Direction::Back));
}

// Reviewer: a drag too short to keep, a click say, leaves no range: the
// mark is where extending starts.
void test_extend_after_a_short_drag()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.start_segment(15);
    d.set_segment(17);
    d.end_segment();
    CHECK(!d.segment.has_value());
    d.set_mark(17);
    CHECK(d.extend(Boundary::Lap, Direction::On));
    CHECK(d.segment.has_value() && d.segment->min == 17 && d.segment->max == 39);
}

// Reviewer: a lap without records, which a watch can log, has no range,
// so extending starts from the mark, and the lap isn't selected after.
void test_extend_from_an_empty_lap()
{
    TestActivity t { { 10, 10, 40, 100 } };
    auto        &d = *t.displayed;
    d.select_lap(1);
    CHECK(!d.segment.has_value());
    CHECK(!d.extend(Boundary::Lap, Direction::On));
    CHECK(d.lap == 1uz);
    d.set_mark(20);
    CHECK(d.extend(Boundary::Lap, Direction::On));
    CHECK(!d.lap.has_value());
    CHECK(d.segment.has_value() && d.segment->min == 20 && d.segment->max == 39);
}

// Reviewer: a mark past the records, left from a longer activity, is
// nothing to start from.
void test_extend_from_a_mark_past_the_records()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.set_mark(100);
    CHECK(!d.can_extend(Boundary::Lap, Direction::Back));
    CHECK(!d.extend(Boundary::Lap, Direction::Back));
    CHECK(!d.segment.has_value());
}

// Reviewer: during a drag, extend() doesn't take over the range being
// dragged, nor end the drag.
void test_extend_while_dragging()
{
    TestActivity t;
    auto        &d = *t.displayed;
    d.start_segment(15);
    d.set_segment(25);
    CHECK(!d.extend(Boundary::Lap, Direction::On));
    CHECK(d.anchor == 15uz);
    CHECK(d.segment.has_value() && d.segment->min == 15 && d.segment->max == 25);
}

// Reviewer: while comparing, the comparison is the extended range's, a
// short one included, and no longer the lap's.
void test_comparison_of_an_extended_range()
{
    TestActivity t;
    auto        &d = *t.displayed;
    for (auto ix = 0uz; ix < d.activity.recorded.size(); ++ix) {
        d.activity.recorded[ix].power = static_cast<u16>(ix < 10 ? 100 : 200);
    }
    auto other = d.activity;
    for (auto &rec : other.recorded) {
        rec.power = static_cast<u16>(*rec.power - 10);
    }
    d.compare_with(other);
    d.select_lap(0);
    CHECK(d.comparison().has_value() && d.comparison()->avg_power.shown == 100.0);
    // Records 0 to 39, 10 at 100 W and 30 at 200 W.
    CHECK(d.extend(Boundary::Lap, Direction::On));
    auto const range = d.comparison();
    CHECK(range.has_value() && range->avg_power.shown == 175.0 && range->avg_power.other == 165.0);

    // Records 38 and 39, two seconds.
    d.whole_activity();
    d.set_mark(38);
    CHECK(d.extend(Boundary::Lap, Direction::On));
    auto const short_range = d.comparison();
    CHECK(short_range.has_value() && short_range->avg_power.shown == 200.0);
}

int main()
{
    test_current_record();
    test_extend_a_range();
    test_extend_a_lap();
    test_extend_from_the_mark();
    test_extend_from_nothing();
    test_extend_to_nothing();
    test_no_extending_while_dragging();
    test_extend_back_from_the_mark();
    test_extend_after_a_short_drag();
    test_extend_from_an_empty_lap();
    test_extend_from_a_mark_past_the_records();
    test_extend_while_dragging();
    test_comparison_of_an_extended_range();
    test_keep_lap();
    test_comparison();
    test_comparison_of_a_lap_by_the_clock();
    test_drag_either_way();
    test_end_segment();
    test_range_stats_cleared();
    test_lap_selected_during_a_drag();
    test_esc_during_a_drag();
    test_range_with_time_going_back();
    test_select_lap();
    test_drag_ends_the_lap();
    test_toggle_lap();
    test_next_and_previous();
    test_one_lap();
    test_empty_lap();
    test_next_over_an_empty_lap();
    test_empty_lap_in_the_strip();
    test_select_lap_replaces_a_dragged_range();
    test_lap_of();
    test_lap_spans();
    test_lap_at();
    test_selection();
    return ST::Test::test_result();
}
