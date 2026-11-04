/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests a run's pace curve, the best of a stretch of runs, and the
 * critical speed fitted to it.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <vector>

#include <storage/PaceCurve.h>

#include <TestApp.h>

using namespace ST;

namespace chrono = std::chrono;

namespace {

constexpr u32 Start = 1'000'000'000;

// The index of `metres` in PaceDistances.
size_t at(u32 metres)
{
    return static_cast<size_t>(std::ranges::find(PaceDistances, metres) - PaceDistances.begin());
}

FIT::record rec(u32 second, std::optional<float> distance)
{
    return FIT::record { .timestamp = DateTime { .timestamp = Start + second }, .distance = distance };
}

// Records every `every` seconds, from `from` for `count` seconds, at
// `speed`, from `distance` on; the distance at the end.
float add(std::vector<FIT::record> &records, u32 from, u32 count, float speed, float distance = 0, u32 every = 1)
{
    for (auto second = 0u; second < count; second += every) {
        records.push_back(rec(from + second, distance + speed * static_cast<float>(second)));
    }
    return distance + speed * static_cast<float>(count);
}

void test_steady_running()
{
    // 4 m/s, 4:10/km, for 25 minutes: 6 km.
    std::vector<FIT::record> records;
    add(records, 0, 1501, 4);
    auto const curve = pace_curve(records);
    CHECK_EQ(curve.size(), at(5000) + 1);
    CHECK_EQ(curve[at(400)], 100u);
    CHECK_EQ(curve[at(1000)], 250u);
    CHECK_EQ(curve[at(1609)], 402u);
    CHECK_EQ(curve[at(5000)], 1250u);
}

void test_the_start_is_interpolated()
{
    // A record every 7 seconds: 400 m is still 100 s, not 105.
    std::vector<FIT::record> records;
    add(records, 0, 1001, 4, 0, 7);
    auto const curve = pace_curve(records);
    CHECK_EQ(curve[at(400)], 100u);
}

void test_a_faster_kilometre()
{
    std::vector<FIT::record> records;
    auto                     distance = add(records, 0, 600, 3);
    distance = add(records, 600, 200, 5, distance);
    add(records, 800, 601, 3, distance);
    auto const curve = pace_curve(records);
    CHECK_EQ(curve[at(400)], 80u);
    CHECK_EQ(curve[at(1000)], 200u);
    // 2 km: the fast kilometre and one at 3 m/s.
    CHECK_EQ(curve[at(2000)], 533u);
}

void test_a_stop_counts_as_time()
{
    std::vector<FIT::record> records;
    auto const               distance = add(records, 0, 125, 4);
    // Half a minute at a traffic light, then on.
    add(records, 155, 126, 4, distance);
    auto const curve = pace_curve(records);
    CHECK_EQ(curve[at(400)], 100u);
    CHECK_EQ(curve[at(1000)], 280u);
}

void test_glitches_dont_count()
{
    std::vector<FIT::record> records;
    auto                     distance = add(records, 0, 100, 4);
    // A jump of 100 m in a second, and the distance going back.
    distance += 100;
    distance = add(records, 100, 50, 4, distance);
    distance -= 50;
    add(records, 150, 200, 4, distance);
    auto const curve = pace_curve(records);
    // None of a glitch's second counts, the 4 m really run in it
    // neither: a kilometre takes in both, and is 2 s slower.
    CHECK_EQ(curve[at(400)], 100u);
    CHECK_EQ(curve[at(1000)], 252u);
    CHECK_EQ(curve.size(), at(1000) + 1);
}

void test_without_distance()
{
    std::vector<FIT::record> records;
    for (auto second = 0u; second < 600; ++second) {
        records.push_back(rec(second, std::nullopt));
    }
    CHECK(pace_curve(records).empty());
    CHECK(pace_curve({ }).empty());
    // Records without a distance in between are passed over.
    records.clear();
    add(records, 0, 50, 4);
    records.push_back(rec(50, std::nullopt));
    add(records, 51, 100, 4, 204);
    auto const curve = pace_curve(records);
    CHECK_EQ(curve.size(), 1uz);
    if (!curve.empty()) {
        CHECK_EQ(curve[at(400)], 100u);
    }
}

void test_shorter_than_a_distance()
{
    std::vector<FIT::record> records;
    add(records, 0, 99, 4);
    CHECK(pace_curve(records).empty());
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

ActivitySummary run(chrono::year_month_day day, std::vector<u32> times, std::optional<FIT::sub_sport> sub_sport = { })
{
    return ActivitySummary { .start = local_noon(day), .sport = FIT::sport::Running, .sub_sport = sub_sport, .best_times = std::move(times) };
}

void test_best_pace_curve()
{
    auto ride = run(date(2026, 3, 5), { 10, 10, 10 });
    ride.sport = FIT::sport::Cycling;
    ActivitySummaries const summaries {
        run(date(2026, 1, 1), { 60, 130 }), // Before the window
        run(date(2026, 3, 1), { 90, 200 }),
        run(date(2026, 3, 2), { 95, 190, 240 }),
        run(date(2026, 3, 3), { 50, 100, 120 }, FIT::sub_sport::treadmill),
        run(date(2026, 3, 4), { 50, 100, 120 }, FIT::sub_sport::indoor_running),
        ride,
        run(date(2026, 3, 6), { 80, 170 }, FIT::sub_sport::trail),
        run(date(2026, 4, 1), { 50 }), // After it
    };
    auto const bests = best_pace_curve(summaries, date(2026, 2, 1), date(2026, 3, 31));
    CHECK_EQ(bests.size(), 3uz);
    if (bests.size() == 3) {
        CHECK(bests[0] == (PaceBest { .seconds = 80, .summary = 6 }));
        CHECK(bests[1] == (PaceBest { .seconds = 170, .summary = 6 }));
        CHECK(bests[2] == (PaceBest { .seconds = 240, .summary = 2 }));
    }
}

// The bests of a runner with a CS of 4 m/s and a D' of 200 m, up to
// `to` metres.
std::vector<PaceBest> modelled(u32 to)
{
    std::vector<PaceBest> ret;
    for (auto const metres : PaceDistances) {
        if (metres > to) {
            break;
        }
        ret.push_back(PaceBest { .seconds = static_cast<u32>(std::lround((metres - 200.0) / 4.0)) });
    }
    return ret;
}

void test_critical_speed()
{
    auto const cs = critical_speed(modelled(10000));
    CHECK(cs.has_value());
    if (cs) {
        CHECK(std::abs(cs->cs - 4) < 0.01);
        CHECK(std::abs(cs->d_prime - 200) < 5);
    }
    // 4 km is 950 s: enough. 3 km, 700 s, isn't.
    CHECK(critical_speed(modelled(4000)).has_value());
    CHECK(!critical_speed(modelled(3000)).has_value());
    CHECK(!critical_speed({ }).has_value());
}

void test_no_critical_speed_from_an_unsound_fit()
{
    // Only two bests from 3 to 20 minutes: 3 km in 15 and 4 km in 20.
    std::vector<PaceBest> two(at(4000) + 1, PaceBest { .seconds = 60 });
    two[at(3000)].seconds = 900;
    two[at(4000)].seconds = 1200;
    CHECK(!critical_speed(two).has_value());
    // Each distance 10 s slower than an even 4 m/s: D' below 0.
    std::vector<PaceBest> slower;
    for (auto const metres : PaceDistances) {
        if (metres > 5000) {
            break;
        }
        slower.push_back(PaceBest { .seconds = metres / 4 + 10 });
    }
    CHECK(!critical_speed(slower).has_value());
}

void test_out_of_order_and_repeated_seconds()
{
    // A second recorded twice, the second time 50 m on, and a record from
    // before the last: neither counts, and neither moves the distance on.
    std::vector<FIT::record> records;
    auto const               distance = add(records, 0, 100, 4);
    records.push_back(rec(99, distance + 46));
    records.push_back(rec(50, 2000));
    add(records, 100, 201, 4, distance);
    auto const curve = pace_curve(records);
    CHECK_EQ(curve.size(), at(1000) + 1);
    if (curve.size() > at(1000)) {
        CHECK_EQ(curve[at(400)], 100u);
        CHECK_EQ(curve[at(1000)], 250u);
    }
}

void test_the_glitch_limit()
{
    // 8 m/s is still running, and the start is interpolated in it.
    std::vector<FIT::record> records;
    add(records, 0, 101, 8);
    auto const curve = pace_curve(records);
    CHECK_EQ(curve.size(), at(800) + 1);
    if (curve.size() > at(800)) {
        CHECK_EQ(curve[at(400)], 50u);
        CHECK_EQ(curve[at(800)], 100u);
    }
    // Records 7 s apart: 35 m in each is 5 m/s, not 35.
    records.clear();
    add(records, 0, 701, 5, 0, 7);
    auto const spaced = pace_curve(records);
    CHECK_EQ(spaced.size(), at(3000) + 1);
    if (!spaced.empty()) {
        CHECK_EQ(spaced[at(400)], 80u);
    }
}

void test_distance_from_a_start_above_zero()
{
    // The first record's distance is where the curve starts from, as a
    // run resumed or a foot pod's count: 1 km on, not 1.5.
    std::vector<FIT::record> records;
    add(records, 0, 251, 4, 500);
    auto const curve = pace_curve(records);
    CHECK_EQ(curve.size(), at(1000) + 1);
    if (curve.size() > at(1000)) {
        CHECK_EQ(curve[at(1000)], 250u);
    }
    // Exactly the distance: the whole run.
    records.clear();
    add(records, 0, 101, 4);
    auto const exactly = pace_curve(records);
    CHECK_EQ(exactly.size(), 1uz);
    if (!exactly.empty()) {
        CHECK_EQ(exactly[0], 100u);
    }
}

void test_a_stop_at_the_start_of_a_best()
{
    // Standing at the start for a minute, then 400 m at 4 m/s: the best
    // starts as the runner sets off.
    std::vector<FIT::record> records;
    add(records, 0, 60, 0);
    add(records, 60, 101, 4);
    auto const curve = pace_curve(records);
    CHECK_EQ(curve.size(), 1uz);
    if (!curve.empty()) {
        CHECK_EQ(curve[0], 100u);
    }
}

void test_critical_speed_bounds()
{
    // From 3 minutes to 20, both in: 800 m in 180 s, 3 km in 900, 4 km
    // in 1200, at a CS of 3.1 m/s or so with a D' above 0.
    std::vector<PaceBest> bests(at(4000) + 1, PaceBest { .seconds = 60 });
    bests[at(800)].seconds = 180;
    bests[at(1000)].seconds = 179;
    bests[at(1500)].seconds = 179;
    bests[at(1609)].seconds = 179;
    bests[at(2000)].seconds = 179;
    bests[at(3000)].seconds = 900;
    bests[at(4000)].seconds = 1200;
    CHECK(critical_speed(bests).has_value());
    // 4 km in 1201 s is out of the fit, which leaves two.
    bests[at(4000)].seconds = 1201;
    CHECK(!critical_speed(bests).has_value());
    // Three in it, the longest 899 s.
    bests[at(4000)].seconds = 1201;
    bests[at(2000)].seconds = 500;
    bests[at(3000)].seconds = 899;
    CHECK(!critical_speed(bests).has_value());
    bests[at(3000)].seconds = 900;
    CHECK(critical_speed(bests).has_value());
}

}

int main()
{
    test_steady_running();
    test_the_start_is_interpolated();
    test_a_faster_kilometre();
    test_a_stop_counts_as_time();
    test_glitches_dont_count();
    test_without_distance();
    test_shorter_than_a_distance();
    test_best_pace_curve();
    test_critical_speed();
    test_no_critical_speed_from_an_unsound_fit();
    test_out_of_order_and_repeated_seconds();
    test_the_glitch_limit();
    test_distance_from_a_start_above_zero();
    test_a_stop_at_the_start_of_a_best();
    test_critical_speed_bounds();
    return Test::test_result();
}
