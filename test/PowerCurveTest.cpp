/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests a ride's power curve, the best of a stretch of rides, and the
 * critical power fitted to it.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <vector>

#include <storage/PowerCurve.h>

#include <TestApp.h>

using namespace ST;

namespace chrono = std::chrono;

namespace {

constexpr u32 Start = 1'000'000'000;

// The index of `seconds` in PowerDurations.
size_t at(u32 seconds)
{
    return static_cast<size_t>(std::ranges::find(PowerDurations, seconds) - PowerDurations.begin());
}

FIT::record rec(u32 second, std::optional<u16> power)
{
    return FIT::record { .timestamp = DateTime { .timestamp = Start + second }, .power = power };
}

// A record a second at `watts`, from `from` for `count` seconds.
void add(std::vector<FIT::record> &records, u32 from, u32 count, std::optional<u16> watts)
{
    for (auto second = from; second < from + count; ++second) {
        records.push_back(rec(second, watts));
    }
}

void test_steady_power()
{
    std::vector<FIT::record> records;
    add(records, 0, 600, 200);
    auto const curve = power_curve(records);
    // The durations up to 10 minutes, and none longer.
    CHECK_EQ(curve.size(), at(600) + 1);
    CHECK(std::ranges::all_of(curve, [](u16 watts) { return watts == 200; }));
}

void test_an_effort_in_the_middle()
{
    std::vector<FIT::record> records;
    add(records, 0, 600, 150);
    add(records, 600, 300, 300);
    add(records, 900, 600, 150);
    auto const curve = power_curve(records);
    CHECK_EQ(curve[0], 300);
    CHECK_EQ(curve[at(300)], 300);
    // Ten minutes: the five at 300 and five at 150.
    CHECK_EQ(curve[at(600)], 225);
}

void test_a_pause_is_zero()
{
    std::vector<FIT::record> records;
    add(records, 0, 60, 300);
    // A minute's café stop, then another minute at 300.
    add(records, 120, 60, 300);
    auto const curve = power_curve(records);
    CHECK_EQ(curve[at(60)], 300);
    // Two minutes: never two at 300 without the stop.
    CHECK_EQ(curve[at(120)], 150);
}

void test_a_short_gap_is_held()
{
    // Smart recording: a record every 5 seconds.
    std::vector<FIT::record> records;
    for (auto second = 0u; second <= 600; second += 5) {
        records.push_back(rec(second, 200));
    }
    auto const curve = power_curve(records);
    CHECK_EQ(curve[at(1)], 200);
    CHECK_EQ(curve[at(600)], 200);
}

void test_spikes_and_missing_power_are_zero()
{
    std::vector<FIT::record> records;
    add(records, 0, 10, 100);
    records.push_back(rec(10, 2000));
    records.push_back(rec(11, std::nullopt));
    add(records, 12, 8, 100);
    auto const curve = power_curve(records);
    CHECK_EQ(curve[0], 100);
    CHECK_EQ(curve[at(20)], 90);
}

void test_without_power()
{
    std::vector<FIT::record> records;
    add(records, 0, 60, std::nullopt);
    CHECK(power_curve(records).empty());
    CHECK(power_curve({ }).empty());
    records.clear();
    add(records, 0, 60, 0);
    CHECK(power_curve(records).empty());
}

void test_out_of_order_records()
{
    std::vector<FIT::record> records;
    add(records, 0, 10, 100);
    records.push_back(rec(5, 900));
    records.push_back(rec(9, 900));
    add(records, 10, 10, 100);
    auto const curve = power_curve(records);
    CHECK_EQ(curve[0], 100);
    CHECK_EQ(curve.size(), at(20) + 1);
}

chrono::year_month_day date(int year, unsigned month, unsigned day)
{
    return chrono::year { year } / chrono::month { month } / chrono::day { day };
}

i64 local_noon(chrono::year_month_day day)
{
    std::tm tm {
        .tm_sec = 0,
        .tm_min = 0,
        .tm_hour = 12,
        .tm_mday = static_cast<int>(static_cast<unsigned>(day.day())),
        .tm_mon = static_cast<int>(static_cast<unsigned>(day.month())) - 1,
        .tm_year = static_cast<int>(day.year()) - 1900,
        .tm_isdst = -1,
    };
    return static_cast<i64>(std::mktime(&tm));
}

ActivitySummary ride(chrono::year_month_day day, std::vector<u16> power, FIT::sport sport = FIT::sport::Cycling)
{
    return ActivitySummary { .start = local_noon(day), .sport = sport, .power = std::move(power) };
}

void test_best_curve()
{
    ActivitySummaries const summaries {
        ride(date(2026, 1, 1), { 900, 800, 700 }), // Before the window
        ride(date(2026, 3, 1), { 500, 400 }),
        ride(date(2026, 3, 2), { 450, 420, 300 }),
        ride(date(2026, 3, 3), { 999, 999, 999, 999 }, FIT::sport::Running),
        ride(date(2026, 3, 4), { }),
        ride(date(2026, 4, 1), { 990 }), // After it
    };
    auto const bests = best_curve(summaries, date(2026, 2, 1), date(2026, 3, 31));
    CHECK_EQ(bests.size(), 3uz);
    if (bests.size() == 3) {
        CHECK(bests[0] == (PowerBest { .watts = 500, .summary = 1 }));
        CHECK(bests[1] == (PowerBest { .watts = 420, .summary = 2 }));
        CHECK(bests[2] == (PowerBest { .watts = 300, .summary = 2 }));
    }
    CHECK(best_curve(summaries, date(2025, 1, 1), date(2025, 12, 31)).empty());
    // Zwift's rides are cycling too, by sub-sport.
    auto indoor = ride(date(2026, 3, 5), { 1000 }, FIT::sport::Training);
    indoor.sub_sport = FIT::sub_sport::indoor_cycling;
    auto const with_indoor = best_curve({ indoor }, date(2026, 3, 1), date(2026, 3, 31));
    CHECK_EQ(with_indoor.size(), 1uz);
}

// The bests of a rider with a CP of 280 W and a W' of 20 kJ, up to `to`.
std::vector<PowerBest> modelled(u32 to)
{
    std::vector<PowerBest> ret;
    for (auto const t : PowerDurations) {
        if (t > to) {
            break;
        }
        ret.push_back(PowerBest { .watts = static_cast<u16>(std::lround(280.0 + 20000.0 / t)) });
    }
    return ret;
}

void test_critical_power()
{
    auto const cp = critical_power(modelled(3600));
    CHECK(cp.has_value());
    if (cp) {
        CHECK(std::abs(cp->cp - 280) < 1);
        CHECK(std::abs(cp->w_prime - 20000) < 300);
    }
    // Without a 20-minute best.
    CHECK(!critical_power(modelled(900)).has_value());
    CHECK(critical_power(modelled(1200)).has_value());
    CHECK(!critical_power({ }).has_value());
}

void test_no_critical_power_from_an_unsound_fit()
{
    // As much power for 20 minutes as for 3: W' would be 0.
    std::vector<PowerBest> flat(at(1200) + 1, PowerBest { .watts = 200 });
    CHECK(!critical_power(flat).has_value());
    // More for longer, which no rider does: W' below zero.
    std::vector<PowerBest> rising;
    for (auto d = 0uz; d <= at(1200); ++d) {
        rising.push_back(PowerBest { .watts = static_cast<u16>(100 + d * 10) });
    }
    CHECK(!critical_power(rising).has_value());
}

// A gap of up to 10 s between timestamps, as hr_load and the ranges'
// moving time have it, is held; 11 s is a pause.
void test_the_gap_limit()
{
    std::vector<FIT::record> held;
    held.push_back(rec(0, 100));
    held.push_back(rec(10, 100));
    auto const held_curve = power_curve(held);
    CHECK_EQ(held_curve.size(), at(10) + 1);
    CHECK_EQ(held_curve[at(10)], 100);
    std::vector<FIT::record> paused;
    paused.push_back(rec(0, 100));
    paused.push_back(rec(11, 100));
    auto const paused_curve = power_curve(paused);
    CHECK_EQ(paused_curve.size(), at(10) + 1);
    // Twelve seconds, ten of them at zero: 10 W over 10 s at best.
    CHECK_EQ(paused_curve[at(10)], 10);
}

void test_records_on_the_same_second()
{
    std::vector<FIT::record> records;
    add(records, 0, 10, 100);
    // The second one of second 5 is passed over.
    records.insert(records.begin() + 6, rec(5, 1000));
    auto const curve = power_curve(records);
    CHECK_EQ(curve.size(), at(10) + 1);
    CHECK_EQ(curve[0], 100);
}

void test_the_spike_limit()
{
    std::vector<FIT::record> records;
    records.push_back(rec(0, MaxPower));
    records.push_back(rec(1, static_cast<u16>(MaxPower + 1)));
    auto const curve = power_curve(records);
    CHECK_EQ(curve.size(), 2uz);
    if (curve.size() == 2) {
        CHECK_EQ(curve[0], MaxPower);
        CHECK_EQ(curve[1], 750);
    }
    // Only spikes: no power.
    CHECK(power_curve(std::vector { rec(0, 2000), rec(1, 2000) }).empty());
}

void test_a_single_record()
{
    auto const curve = power_curve(std::vector { rec(0, 250) });
    CHECK_EQ(curve.size(), 1uz);
    CHECK(curve == std::vector<u16> { 250 });
}

// Half a watt rounds up.
void test_rounding()
{
    std::vector<FIT::record> records;
    records.push_back(rec(0, 100));
    records.push_back(rec(1, 101));
    auto const curve = power_curve(records);
    CHECK_EQ(curve.size(), 2uz);
    if (curve.size() == 2) {
        CHECK_EQ(curve[1], 101);
    }
}

// The window's first and last days are in it; a tie stays with the
// earlier ride.
void test_best_curve_bounds()
{
    ActivitySummaries const summaries {
        ride(date(2026, 2, 28), { 999 }),
        ride(date(2026, 3, 1), { 500 }),
        ride(date(2026, 3, 15), { 500, 400 }),
        ride(date(2026, 3, 31), { 450, 450, 450 }),
        ride(date(2026, 4, 1), { 999, 999, 999, 999 }),
    };
    auto const bests = best_curve(summaries, date(2026, 3, 1), date(2026, 3, 31));
    CHECK_EQ(bests.size(), 3uz);
    if (bests.size() == 3) {
        CHECK(bests[0] == (PowerBest { .watts = 500, .summary = 1 }));
        CHECK(bests[1] == (PowerBest { .watts = 450, .summary = 3 }));
        CHECK(bests[2] == (PowerBest { .watts = 450, .summary = 3 }));
    }
    auto const one_day = best_curve(summaries, date(2026, 3, 15), date(2026, 3, 15));
    CHECK_EQ(one_day.size(), 2uz);
}

// Only the bests from 3 to 20 minutes count: a sprinter's 5 s or a long
// ride's hour don't move CP.
void test_critical_power_ignores_other_durations()
{
    auto       bests = modelled(3600);
    auto const before = critical_power(bests);
    bests[0].watts = 1500;
    bests[at(3600)].watts = 100;
    auto const after = critical_power(bests);
    CHECK(before && after && *before == *after);
}

}

int main()
{
    test_steady_power();
    test_an_effort_in_the_middle();
    test_a_pause_is_zero();
    test_a_short_gap_is_held();
    test_spikes_and_missing_power_are_zero();
    test_without_power();
    test_out_of_order_records();
    test_best_curve();
    test_critical_power();
    test_no_critical_power_from_an_unsound_fit();
    test_the_gap_limit();
    test_records_on_the_same_second();
    test_the_spike_limit();
    test_a_single_record();
    test_rounding();
    test_best_curve_bounds();
    test_critical_power_ignores_other_durations();
    return Test::test_result();
}
