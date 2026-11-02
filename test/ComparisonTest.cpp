/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
#include <optional>
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
    test_compare_paired_seconds_only();
    test_compare_cadence_leaves_out_zeros();
    test_compare_np();
    test_compare_one_sided();
    test_compared_sign();
    return Test::test_result();
}
