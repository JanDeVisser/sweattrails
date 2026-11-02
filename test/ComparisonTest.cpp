/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
#include <optional>
#include <span>
#include <vector>

#include <storage/Comparison.h>

#include <TestApp.h>

using namespace ST;

namespace {

using Records = std::vector<FIT::record>;
using Pairs = std::vector<std::optional<size_t>>;

// `count` records from `start`, one every `every` seconds.
Records records(u32 start, size_t count, u32 every = 1)
{
    Records ret(count);
    for (auto ix = 0uz; ix < count; ++ix) {
        ret[ix].timestamp.timestamp = start + static_cast<u32>(ix) * every;
    }
    return ret;
}

int index(std::optional<size_t> ix)
{
    return ix ? static_cast<int>(*ix) : -1;
}

bool near(std::optional<float64> value, float64 expected)
{
    return value && std::abs(*value - expected) < 1e-6;
}

// A head unit started a minute after the watch: the watch's first minute
// has no pair, and after that each second pairs with the same second.
void test_pair_started_late()
{
    auto const watch = records(1000, 120);
    auto const head_unit = records(1060, 60);
    auto const pairs = pair_records(watch, head_unit);
    CHECK_EQ(pairs.size(), 120uz);
    CHECK_EQ(index(pairs[0]), -1);
    CHECK_EQ(index(pairs[59]), -1);
    CHECK_EQ(index(pairs[60]), 0);
    CHECK_EQ(index(pairs[119]), 59);
    // The other way around.
    auto const back = pair_records(head_unit, watch);
    CHECK_EQ(index(back[0]), 60);
}

// Smart recording: one record every 3 s holds for 2 s after it, so the
// third second has no pair.
void test_pair_smart_recording()
{
    auto const every_second = records(1000, 7);
    auto const every_three = records(1000, 3, 3); // 1000, 1003, 1006
    auto const pairs = pair_records(every_second, every_three);
    CHECK((pairs == Pairs { 0, 0, 0, 1, 1, 1, 2 }));
    auto const sparse = records(1000, 3, 4); // 1000, 1004, 1008
    CHECK((pair_records(every_second, sparse) == Pairs { 0, 0, 0, std::nullopt, 1, 1, 1 }));
}

void test_pair_nothing_in_common()
{
    auto const morning = records(1000, 10);
    auto const evening = records(50000, 10);
    for (auto const &pair : pair_records(morning, evening)) {
        CHECK(!pair.has_value());
    }
    CHECK(pair_records(morning, { }).size() == 10);
    CHECK(pair_records({ }, morning).empty());
}

// Reviewer: a timestamp that repeats in the other pairs with the last
// record of that second, and one in the recording on display pairs both.
void test_pair_repeated_timestamps()
{
    Records other = records(1000, 4);          // 1000, 1001, 1002, 1003
    other.insert(other.begin() + 2, other[1]); // 1000, 1001, 1001, 1002, 1003
    auto const shown = records(1000, 4);
    CHECK((pair_records(shown, other) == Pairs { 0, 2, 3, 4 }));
    Records twice = records(1000, 3);
    twice.insert(twice.begin() + 1, twice[1]); // 1000, 1001, 1001, 1002
    CHECK((pair_records(twice, records(1000, 3)) == Pairs { 0, 1, 1, 2 }));
}

// Review finding 5: the watch on display sets its clock back 2 s, as
// watches do (DisplayedActivity::end_segment() allows for it). Its
// record at 1004 the second time has the other's at the same second, but
// pair_records() has moved past it to 1005, and the u32 subtraction
// t - other[next - 1] wraps around, so it gets no pair.
void test_pair_clock_set_back()
{
    Records shown = records(1000, 6); // 1000 to 1005
    for (auto const &rec : records(1004, 4)) {
        shown.push_back(rec); // 1004 to 1007
    }
    auto const other = records(1000, 8); // 1000 to 1007
    auto const pairs = pair_records(shown, other);
    CHECK_EQ(pairs.size(), 10uz);
    if (pairs.size() == 10) {
        CHECK_EQ(index(pairs[6]), 4);
        CHECK_EQ(index(pairs[7]), 5);
        CHECK_EQ(index(pairs[9]), 7);
    }
}

// Two power meters, 250 and 240 W, with a dropout on the other: those
// seconds don't count, so the shown one's average isn't dragged along.
void test_compare_paired_seconds_only()
{
    auto shown = records(1000, 60);
    auto other = records(1000, 60);
    for (auto ix = 0uz; ix < 60; ++ix) {
        shown[ix].power = ix < 30 ? 200 : 300; // Average 250
        other[ix].power = ix < 30 ? 192 : 288; // Average 240
        shown[ix].heart_rate = 140;
        other[ix].heart_rate = ix == 10 ? 160 : 142;
    }
    auto const pairs = pair_records(shown, other);
    auto const both = compare(shown, other, pairs, 0, 60);
    CHECK(near(both.avg_power.shown, 250));
    CHECK(near(both.avg_power.other, 240));
    CHECK(near(both.avg_power.difference(), 10));
    CHECK(near(both.avg_power.percent(), 10.0 / 240.0 * 100.0));
    CHECK(near(both.max_power.shown, 300) && near(both.max_power.other, 288));
    CHECK(near(both.max_hr.other, 160));
    CHECK(near(both.max_hr.difference(), -20));

    // A dropout of the other's power in the second half.
    for (auto ix = 40uz; ix < 60; ++ix) {
        other[ix].power.reset();
    }
    auto const dropout = compare(shown, other, pairs, 0, 60);
    CHECK(near(dropout.avg_power.shown, (30 * 200.0 + 10 * 300.0) / 40.0));
    CHECK(near(dropout.avg_power.other, (30 * 192.0 + 10 * 288.0) / 40.0));
    // Heart rate isn't affected.
    CHECK(near(dropout.avg_hr.shown, 140));

    // A lap: records [30, 40).
    auto const lap = compare(shown, other, pairs, 30, 40);
    CHECK(near(lap.avg_power.shown, 300) && near(lap.avg_power.other, 288));
}

// Coasting doesn't count in the cadence, on either.
void test_compare_cadence_leaves_out_zeros()
{
    auto shown = records(1000, 10);
    auto other = records(1000, 10);
    for (auto ix = 0uz; ix < 10; ++ix) {
        shown[ix].cadence = ix < 5 ? 90 : 0;
        other[ix].cadence = ix == 0 ? 0 : 88;
    }
    auto const both = compare(shown, other, pair_records(shown, other), 0, 10);
    CHECK(near(both.avg_cadence.shown, 90));
    CHECK(near(both.avg_cadence.other, 88));
}

void test_compare_np()
{
    auto shown = records(1000, 31);
    auto other = records(1000, 31);
    for (auto ix = 0uz; ix < 31; ++ix) {
        shown[ix].power = 250;
        other[ix].power = 240;
    }
    auto const pairs = pair_records(shown, other);
    auto const both = compare(shown, other, pairs, 0, 31);
    CHECK(near(both.np.shown, 250) && near(both.np.other, 240));
    // 30 paired seconds aren't enough.
    auto const short_one = compare(shown, other, pairs, 0, 30);
    CHECK(!short_one.np.shown && !short_one.np.other);
}

// A value only one of them has is that one's, with nothing to compare it
// with: "250 / – W".
void test_compare_one_sided()
{
    auto shown = records(1000, 10);
    auto other = records(1000, 10);
    for (auto ix = 0uz; ix < 10; ++ix) {
        shown[ix].power = 250;
        other[ix].heart_rate = 140;
    }
    auto const both = compare(shown, other, pair_records(shown, other), 0, 10);
    CHECK(near(both.avg_power.shown, 250) && !both.avg_power.other);
    CHECK(!both.avg_power.difference() && !both.avg_power.percent());
    CHECK(!both.avg_hr.shown && near(both.avg_hr.other, 140));
    CHECK(!both.np.shown && !both.np.other);
    // Not in time with each other at all.
    auto const apart = records(50000, 10);
    auto const alone = compare(shown, apart, pair_records(shown, apart), 0, 10);
    CHECK(near(alone.avg_power.shown, 250) && !alone.avg_power.other);
    // Once they both have it for one second, only that second counts.
    other[3].power = 240;
    auto const one = compare(shown, other, pair_records(shown, other), 0, 10);
    CHECK(near(one.avg_power.shown, 250) && near(one.avg_power.other, 240));
}

// Review finding 2: two heart rate straps that never recorded the same
// second, one in the first half and one in the second. No second counts
// for both, so there's nothing to take a difference of; but the fallback
// to each one's own values gives both, and difference() subtracts one
// half's average from the other's: "150 / 140 bpm · +10 bpm (+7%)".
void test_compare_never_together_has_no_difference()
{
    auto shown = records(1000, 10);
    auto other = records(1000, 10);
    for (auto ix = 0uz; ix < 10; ++ix) {
        if (ix < 5) {
            shown[ix].heart_rate = 150;
        } else {
            other[ix].heart_rate = 140;
        }
    }
    auto const both = compare(shown, other, pair_records(shown, other), 0, 10);
    CHECK(!both.avg_hr.difference());
    CHECK(!both.max_hr.difference());
}

// Review finding 3: the activity's own cadence (cadence_stats() in
// storage/Activity.cpp) leaves out records under PedalingCadence (20 rpm)
// or under PedalingPower (20 W), not only the zeros. Ten seconds at
// 10 rpm and 10 W and ten at 90 rpm: the panel says 90 rpm without
// comparing, and 50 rpm for the same recording with Ctrl+K.
void test_compare_cadence_as_the_activity_does()
{
    auto shown = records(1000, 20);
    for (auto ix = 0uz; ix < 20; ++ix) {
        shown[ix].cadence = static_cast<u8>(ix < 10 ? 10 : 90);
        shown[ix].power = static_cast<u16>(ix < 10 ? 10 : 200);
    }
    auto const other = shown;
    auto const both = compare(shown, other, pair_records(shown, other), 0, 20);
    CHECK(near(both.avg_cadence.shown, 90));
    CHECK(near(both.avg_cadence.other, 90));
}

// Reviewer: [first, end) at the ends: an end past the records, an empty
// range, and a first past the end.
void test_compare_ranges_at_the_ends()
{
    auto shown = records(1000, 10);
    auto other = records(1000, 10);
    for (auto ix = 0uz; ix < 10; ++ix) {
        shown[ix].power = static_cast<u16>(100 + ix);
        other[ix].power = 100;
    }
    auto const pairs = pair_records(shown, other);
    auto const past = compare(shown, other, pairs, 5, 1000);
    CHECK(near(past.avg_power.shown, 107) && near(past.max_power.shown, 109));
    auto const last = compare(shown, other, pairs, 9, 10);
    CHECK(near(last.avg_power.shown, 109) && near(last.avg_power.other, 100));
    auto const empty = compare(shown, other, pairs, 4, 4);
    CHECK(!empty.avg_power.shown && !empty.avg_power.other && !empty.max_power.shown);
    auto const backwards = compare(shown, other, pairs, 8, 3);
    CHECK(!backwards.avg_power.shown && !backwards.avg_power.other);
    // Pairs shorter than the records: the records past them don't count.
    auto const fewer = compare(shown, other, std::span { pairs }.first(5), 0, 10);
    CHECK(near(fewer.max_power.shown, 104));
}

// Reviewer: NP only over the seconds both have power: the other's first
// five seconds are missing, so 35 of 40 count, and a spike in them on the
// shown one doesn't.
void test_compare_np_over_paired_seconds()
{
    auto shown = records(1000, 40);
    auto other = records(1000, 40);
    for (auto ix = 0uz; ix < 40; ++ix) {
        shown[ix].power = static_cast<u16>(ix < 5 ? 1000 : 250);
        if (ix >= 5) {
            other[ix].power = 240;
        }
    }
    auto const both = compare(shown, other, pair_records(shown, other), 0, 40);
    CHECK(near(both.np.shown, 250) && near(both.np.other, 240));
    CHECK(near(both.max_power.shown, 250));
}

// Reviewer: smart recording on the other: its record holds for two
// seconds, so its values count for each of the shown one's seconds.
void test_compare_holds_the_others_values()
{
    auto shown = records(1000, 6);
    auto other = records(1000, 2, 3); // 1000, 1003
    for (auto &rec : shown) {
        rec.power = 200;
    }
    other[0].power = 180;
    other[1].power = 210;
    auto const both = compare(shown, other, pair_records(shown, other), 0, 6);
    CHECK(near(both.avg_power.other, 195));
    CHECK(near(both.avg_power.shown, 200));
}

void test_compared_sign()
{
    Compared const lower { .shown = 230, .other = 240 };
    CHECK(near(lower.difference(), -10));
    CHECK(near(lower.percent(), -10.0 / 240.0 * 100.0));
    Compared const zero { .shown = 10, .other = 0 };
    CHECK(near(zero.difference(), 10));
    CHECK(!zero.percent());
}

}

int main()
{
    test_pair_started_late();
    test_pair_smart_recording();
    test_pair_nothing_in_common();
    test_pair_repeated_timestamps();
    test_compare_paired_seconds_only();
    test_compare_cadence_leaves_out_zeros();
    test_compare_cadence_as_the_activity_does();
    test_compare_np();
    test_compare_one_sided();
    test_compare_never_together_has_no_difference();
    test_compare_ranges_at_the_ends();
    test_compare_np_over_paired_seconds();
    test_compare_holds_the_others_values();
    test_compared_sign();
    return Test::test_result();
}
