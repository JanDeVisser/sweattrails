/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the thresholds from the settings, the training stress they score,
 * and fitness, fatigue and form from it.
 */

#include <chrono>
#include <cmath>
#include <ctime>

#include <storage/Fitness.h>
#include <storage/Thresholds.h>

#include <TestApp.h>

using namespace ST;

namespace chrono = std::chrono;

namespace {

chrono::year_month_day date(int year, unsigned month, unsigned day)
{
    return chrono::year { year } / chrono::month { month } / chrono::day { day };
}

// Noon on a day, in the computer's time zone, as the store goes by.
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

// An hour at an NP of `watts`: at an FTP of 250, TSS 100 for 250 W.
ActivitySummary ride(chrono::year_month_day day, u16 watts = 250)
{
    return ActivitySummary {
        .start = local_noon(day),
        .sport = FIT::sport::Cycling,
        .moving = 3600,
        .np = watts,
    };
}

// An hour at 170 bpm: hrTSS 100 at running's LTHR of 170.
ActivitySummary run(chrono::year_month_day day)
{
    return ActivitySummary {
        .start = local_noon(day),
        .sport = FIT::sport::Running,
        .moving = 3600,
        .hr_load = 170.0 * 170 * 3600,
    };
}

ThresholdList thresholds()
{
    return ThresholdList { Thresholds { .from = date(2020, 1, 1), .ftp = 250, .lthr_running = 170, .lthr_cycling = 160 } };
}

bool near(double a, double b)
{
    return std::abs(a - b) < 1e-6;
}

std::expected<ThresholdList, std::string> decode(std::string_view json)
{
    auto const value = JSONValue::deserialize(json);
    if (!value) {
        return std::unexpected(std::string { "not JSON" });
    }
    return decode_thresholds(*value);
}

void test_decode_thresholds()
{
    auto const list = decode(R"([
        { "from": "2012-01-01", "ftp": 240, "lthr_running": 170, "lthr_cycling": 162 },
        { "from": "2024-03-01", "ftp": 275 }
    ])");
    CHECK(list.has_value());
    if (!list || list->size() != 2) {
        CHECK_EQ(list ? list->size() : 0uz, 2uz);
        return;
    }
    CHECK(((*list)[0] == Thresholds { .from = date(2012, 1, 1), .ftp = 240, .lthr_running = 170, .lthr_cycling = 162 }));
    CHECK(((*list)[1] == Thresholds { .from = date(2024, 3, 1), .ftp = 275 }));

    auto const none = decode_thresholds(std::nullopt);
    CHECK(none.has_value() && none->empty());
}

void test_bad_thresholds()
{
    CHECK(!decode(R"({ "ftp": 250 })"));
    CHECK(!decode(R"([ { "ftp": 250 } ])"));
    CHECK(!decode(R"([ { "from": "2024-02-30", "ftp": 250 } ])"));
    CHECK(!decode(R"([ { "from": "March 2024", "ftp": 250 } ])"));
    CHECK(!decode(R"([ { "from": "2024-03-01", "ftp": 0 } ])"));
    CHECK(!decode(R"([ { "from": "2024-03-01", "ftp": "fast" } ])"));
    // Out of order.
    CHECK(!decode(R"([ { "from": "2024-03-01", "ftp": 250 }, { "from": "2023-03-01", "ftp": 240 } ])"));
    auto const error = decode(R"([ { "from": "2024-03-01" }, { "from": "2025-03-01", "lthr_running": -1 } ])");
    CHECK(!error && error.error() == "threshold 2's `lthr_running` isn't a number above 0");
}

void test_thresholds_on()
{
    ThresholdList const list {
        Thresholds { .from = date(2012, 1, 1), .ftp = 240, .lthr_running = 170, .lthr_cycling = 162 },
        Thresholds { .from = date(2024, 3, 1), .ftp = 275 },
        Thresholds { .from = date(2025, 1, 1), .lthr_running = 175 },
    };
    // The first counts back.
    CHECK(thresholds_on(list, date(2010, 6, 1))->ftp == 240.0);
    CHECK(thresholds_on(list, date(2024, 2, 29))->ftp == 240.0);
    auto const march = thresholds_on(list, date(2024, 3, 1));
    CHECK(march->ftp == 275.0);
    CHECK(march->lthr_running == 170.0);
    CHECK(march->lthr_cycling == 162.0);
    auto const later = thresholds_on(list, date(2026, 1, 1));
    CHECK(later->ftp == 275.0);
    CHECK(later->lthr_running == 175.0);
    CHECK(!thresholds_on({ }, date(2026, 1, 1)));
}

void test_training_stress()
{
    auto const t = thresholds().front();
    // An hour at threshold is 100, by power or by heart rate.
    auto const power = training_stress(250.0, 3600, { }, FIT::sport::Cycling, { }, t);
    CHECK(power && near(power->value, 100) && !power->heart_rate);
    auto const run = training_stress({ }, 3600, 170.0 * 170 * 3600, FIT::sport::Running, { }, t);
    CHECK(run && near(run->value, 100) && run->heart_rate);
    // Cycling's LTHR for a ride, a spin bike's too; running's for a hike.
    auto const ride = training_stress({ }, 3600, 160.0 * 160 * 3600, FIT::sport::Cycling, { }, t);
    CHECK(ride && near(ride->value, 100));
    auto const spin = training_stress({ }, 3600, 160.0 * 160 * 3600, FIT::sport::Fitness_equipment, FIT::sub_sport::indoor_cycling, t);
    CHECK(spin && near(spin->value, 100));
    auto const hike = training_stress({ }, 3600, 170.0 * 170 * 3600, FIT::sport::Hiking, { }, t);
    CHECK(hike && near(hike->value, 100));
    // Power before heart rate.
    auto const both = training_stress(125.0, 3600, 170.0 * 170 * 3600, FIT::sport::Cycling, { }, t);
    CHECK(both && near(both->value, 25) && !both->heart_rate);
    // Power without an FTP falls back to heart rate.
    auto no_ftp = t;
    no_ftp.ftp.reset();
    auto const fallback = training_stress(250.0, 3600, 160.0 * 160 * 3600, FIT::sport::Cycling, { }, no_ftp);
    CHECK(fallback && fallback->heart_rate);
    CHECK(!training_stress({ }, 3600, { }, FIT::sport::Running, { }, t));
    CHECK(!training_stress(250.0, 3600, { }, FIT::sport::Cycling, { }, std::nullopt));
    CHECK(intensity_factor(200.0, t) == 0.8);
    CHECK(!intensity_factor(200.0, no_ftp));
}

// One activity of 100: CTL and ATL rise by k, and decay by 1 - k a day.
void test_fitness_days()
{
    auto const days = fitness_days({ ride(date(2025, 3, 1)) }, thresholds(), date(2025, 3, 10));
    CHECK_EQ(days.size(), 10uz);
    if (days.size() != 10) {
        return;
    }
    auto const k_ctl = 1 - std::exp(-1.0 / 42);
    auto const k_atl = 1 - std::exp(-1.0 / 7);
    CHECK(days[0].day == date(2025, 3, 1));
    CHECK(near(days[0].tss, 100));
    CHECK(near(days[0].ctl, 100 * k_ctl));
    CHECK(near(days[0].atl, 100 * k_atl));
    CHECK(near(days[0].tsb, 0));
    // The rest days decay.
    CHECK(near(days[1].tss, 0));
    CHECK(near(days[1].ctl, 100 * k_ctl * (1 - k_ctl)));
    CHECK(near(days[9].atl, 100 * k_atl * std::pow(1 - k_atl, 9)));
    // Going into the day: yesterday's.
    CHECK(near(days[1].tsb, days[0].ctl - days[0].atl));
    CHECK(days[1].tsb < 0);
}

void test_activities_on_a_day_add_up()
{
    auto const days = fitness_days({ ride(date(2025, 3, 1)), run(date(2025, 3, 1)), ride(date(2025, 3, 3), 125) }, thresholds(), date(2025, 3, 3));
    CHECK_EQ(days.size(), 3uz);
    if (days.size() == 3) {
        CHECK(near(days[0].tss, 200));
        CHECK(near(days[2].tss, 25));
    }
}

// Without thresholds, or an activity's numbers, a day is rest.
void test_rest_without_stress()
{
    auto const days = fitness_days({ ride(date(2025, 3, 1)) }, { }, date(2025, 3, 2));
    CHECK_EQ(days.size(), 2uz);
    if (days.size() == 2) {
        CHECK(near(days[0].tss, 0));
        CHECK(near(days[1].ctl, 0));
    }
    CHECK(fitness_days({ }, thresholds(), date(2025, 3, 2)).empty());
    CHECK(fitness_days({ ride(date(2025, 3, 1)) }, thresholds(), date(2025, 2, 28)).empty());
}

void test_ramp_rate()
{
    ActivitySummaries summaries;
    for (auto day = 1u; day <= 20; ++day) {
        summaries.push_back(ride(date(2025, 3, day)));
    }
    auto const days = fitness_days(summaries, thresholds(), date(2025, 3, 20));
    CHECK(near(ramp_rate(days, 19), days[19].ctl - days[12].ctl));
    // Before the first week, from 0.
    CHECK(near(ramp_rate(days, 3), days[3].ctl));
    CHECK(near(ramp_rate(days, 100), 0));
}

// 2025 starts on a Wednesday: its first week's Monday is the 6th. Up to
// today, Wednesday the 22nd of January: three weeks and today's.
void test_fitness_weeks()
{
    auto const days = fitness_days({ ride(date(2024, 12, 30)), ride(date(2025, 1, 6)), ride(date(2025, 1, 12)), ride(date(2025, 1, 21)) },
        thresholds(), date(2025, 1, 22));
    auto const weeks = fitness_weeks(days, 2025);
    CHECK_EQ(weeks.size(), 3uz);
    if (weeks.size() != 3) {
        return;
    }
    CHECK(weeks[0].monday == date(2025, 1, 6));
    CHECK(near(weeks[0].tss, 200));
    CHECK(weeks[0].end.day == date(2025, 1, 12));
    CHECK(near(weeks[1].tss, 0));
    CHECK(weeks[2].monday == date(2025, 1, 20));
    CHECK(near(weeks[2].tss, 100));
    CHECK(weeks[2].end.day == date(2025, 1, 22));
    // The week of the 30th of December is 2024's.
    auto const before = fitness_weeks(days, 2024);
    CHECK_EQ(before.size(), 1uz);
    CHECK(fitness_weeks(days, 2026).empty());
}

void test_without_stress()
{
    auto bare = run(date(2025, 5, 1));
    bare.hr_load.reset();
    auto unreadable = bare;
    unreadable.sport.reset();
    ActivitySummaries const summaries { ride(date(2025, 4, 1)), bare, unreadable, run(date(2025, 5, 2)), bare };
    CHECK_EQ(without_stress(summaries, thresholds(), 2025), 2uz);
    CHECK_EQ(without_stress(summaries, thresholds(), 2024), 0uz);
    CHECK_EQ(without_stress(summaries, { }, 2025), 4uz);
}

void test_fitness_years()
{
    auto unreadable = run(date(2019, 5, 1));
    unreadable.sport.reset();
    CHECK((fitness_years({ unreadable, run(date(2021, 5, 1)), ride(date(2023, 1, 1)) }, 2026) == std::pair { 2021, 2026 }));
    CHECK((fitness_years({ }, 2026) == std::pair { 2026, 2026 }));
}

}

int main()
{
    test_decode_thresholds();
    test_bad_thresholds();
    test_thresholds_on();
    test_training_stress();
    test_fitness_days();
    test_activities_on_a_day_add_up();
    test_rest_without_stress();
    test_ramp_rate();
    test_fitness_weeks();
    test_without_stress();
    test_fitness_years();
    return Test::test_result();
}
