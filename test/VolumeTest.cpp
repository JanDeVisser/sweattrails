/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the volume totals: which sport an activity counts for, and the
 * totals per week, month and year.
 */

#include <chrono>
#include <ctime>

#include <storage/Volume.h>

#include <TestApp.h>

using namespace ST;

namespace chrono = std::chrono;

namespace {

// Noon on a day, in the computer's time zone, as the store goes by.
i64 local_noon(int year, unsigned month, unsigned day)
{
    std::tm tm {
        .tm_sec = 0,
        .tm_min = 0,
        .tm_hour = 12,
        .tm_mday = static_cast<int>(day),
        .tm_mon = static_cast<int>(month) - 1,
        .tm_year = year - 1900,
        .tm_isdst = -1,
    };
    return static_cast<i64>(std::mktime(&tm));
}

ActivitySummary ride(int year, unsigned month, unsigned day, float64 km = 40, u32 ascent = 300)
{
    return ActivitySummary {
        .start = local_noon(year, month, day),
        .sport = FIT::sport::Cycling,
        .distance = km * 1000,
        .moving = 3600,
        .ascent = ascent,
    };
}

ActivitySummary run(int year, unsigned month, unsigned day)
{
    return ActivitySummary {
        .start = local_noon(year, month, day),
        .sport = FIT::sport::Running,
        .distance = 10000,
        .moving = 3000,
        .ascent = 50,
    };
}

chrono::year_month_day date(int year, unsigned month, unsigned day)
{
    return chrono::year { year } / chrono::month { month } / chrono::day { day };
}

void test_volume_sport()
{
    using FIT::sport;
    using FIT::sub_sport;
    CHECK(volume_sport(sport::Running, { }) == VolumeSport::Running);
    CHECK(volume_sport(sport::Running, sub_sport::treadmill) == VolumeSport::Running);
    CHECK(volume_sport(sport::Running, sub_sport::trail) == VolumeSport::Running);
    CHECK(volume_sport(sport::Cycling, sub_sport::virtual_activity) == VolumeSport::Cycling);
    CHECK(volume_sport(sport::Cycling, sub_sport::indoor_cycling) == VolumeSport::Cycling);
    // As older Garmins record a spin bike, and a treadmill.
    CHECK(volume_sport(sport::Fitness_equipment, sub_sport::indoor_cycling) == VolumeSport::Cycling);
    CHECK(volume_sport(sport::Fitness_equipment, sub_sport::spin) == VolumeSport::Cycling);
    CHECK(volume_sport(sport::Fitness_equipment, sub_sport::treadmill) == VolumeSport::Running);
    CHECK(!volume_sport(sport::Walking, { }));
    CHECK(!volume_sport(sport::Swimming, sub_sport::lap_swimming));
    // A file that couldn't be read.
    CHECK(!volume_sport({ }, { }));
}

void test_names()
{
    CHECK_EQ(std::string { Period_name(Period::Week) }, "week");
    CHECK(Period_by_name("month") == Period::Month);
    CHECK(Metric_by_name("ascent") == Metric::Ascent);
    CHECK(VolumeSport_by_name("cycling") == VolumeSport::Cycling);
    CHECK(!Metric_by_name("pace"));
}

// The weeks whose Monday is in the year: 2024 starts on a Monday and has
// 53, 2025 52, the first of them starting on 6 January.
void test_weeks()
{
    auto const weeks_2024 = volume_totals({ }, VolumeSport::Cycling, Period::Week, 2024);
    CHECK_EQ(weeks_2024.size(), 53uz);
    CHECK(weeks_2024.front().first_day == date(2024, 1, 1));
    CHECK(weeks_2024.back().first_day == date(2024, 12, 30));
    CHECK(weeks_2024.back().last_day == date(2025, 1, 5));

    auto const weeks_2025 = volume_totals({ }, VolumeSport::Cycling, Period::Week, 2025);
    CHECK_EQ(weeks_2025.size(), 52uz);
    CHECK(weeks_2025.front().first_day == date(2025, 1, 6));
    CHECK(weeks_2025.front().last_day == date(2025, 1, 12));
}

void test_week_totals()
{
    ActivitySummaries const summaries {
        ride(2025, 1, 6, 40, 300),  // Monday: week 0
        ride(2025, 1, 12, 60, 500), // Sunday: still week 0
        run(2025, 1, 8),            // Not a ride
        ride(2025, 1, 13),          // Monday: week 1
    };
    auto const weeks = volume_totals(summaries, VolumeSport::Cycling, Period::Week, 2025);
    CHECK_EQ(weeks[0].count, 2uz);
    CHECK_EQ(weeks[0].distance, 100000.0);
    CHECK_EQ(weeks[0].moving, 7200u);
    CHECK_EQ(weeks[0].ascent, 800u);
    CHECK_EQ(weeks[1].count, 1uz);
    CHECK_EQ(weeks[2].count, 0uz); // A week off
    CHECK_EQ(weeks[0].value(Metric::Distance), 100000.0);
    CHECK_EQ(weeks[0].value(Metric::Time), 7200.0);
    CHECK_EQ(weeks[0].value(Metric::Count), 2.0);
    CHECK_EQ(weeks[0].value(Metric::Ascent), 800.0);
}

// The first days of 2026 are in the week of Monday 29 December 2025, so
// they're in 2025's weeks, and in none of 2026's.
void test_week_across_new_year()
{
    ActivitySummaries const summaries { ride(2026, 1, 2) };
    auto const              weeks_2025 = volume_totals(summaries, VolumeSport::Cycling, Period::Week, 2025);
    CHECK(weeks_2025.back().first_day == date(2025, 12, 29));
    CHECK_EQ(weeks_2025.back().count, 1uz);
    auto const weeks_2026 = volume_totals(summaries, VolumeSport::Cycling, Period::Week, 2026);
    auto const in_2026 = std::ranges::fold_left(weeks_2026, 0uz, [](size_t n, PeriodTotal const &week) { return n + week.count; });
    CHECK_EQ(in_2026, 0uz);
    // But in January 2026.
    auto const months_2026 = volume_totals(summaries, VolumeSport::Cycling, Period::Month, 2026);
    CHECK_EQ(months_2026[0].count, 1uz);
}

void test_months()
{
    ActivitySummaries const summaries { ride(2024, 12, 31), ride(2025, 1, 1), ride(2025, 2, 28), ride(2025, 12, 31) };
    auto const              months = volume_totals(summaries, VolumeSport::Cycling, Period::Month, 2025);
    CHECK_EQ(months.size(), 12uz);
    CHECK(months[1].first_day == date(2025, 2, 1));
    CHECK(months[1].last_day == date(2025, 2, 28));
    CHECK_EQ(months[0].count, 1uz);
    CHECK_EQ(months[1].count, 1uz);
    CHECK_EQ(months[11].count, 1uz);
    CHECK_EQ(months[5].count, 0uz);
}

// From the sport's first activity to the year asked for, also years
// without any in between.
void test_years()
{
    ActivitySummaries const summaries { run(2009, 6, 1), ride(2010, 5, 1), ride(2013, 5, 1), ride(2013, 7, 1) };
    auto const              years = volume_totals(summaries, VolumeSport::Cycling, Period::Year, 2015);
    CHECK_EQ(years.size(), 6uz);
    CHECK(years.front().first_day == date(2010, 1, 1));
    CHECK(years.back().last_day == date(2015, 12, 31));
    CHECK_EQ(years[0].count, 1uz);
    CHECK_EQ(years[1].count, 0uz);
    CHECK_EQ(years[3].count, 2uz);
    CHECK_EQ(volume_totals(summaries, VolumeSport::Running, Period::Year, 2015).size(), 7uz);
}

void test_without_distance()
{
    auto treadmill = run(2025, 3, 3);
    treadmill.distance = 0;
    auto const months = volume_totals({ treadmill }, VolumeSport::Running, Period::Month, 2025);
    CHECK_EQ(months[2].count, 1uz);
    CHECK_EQ(months[2].distance, 0.0);
    CHECK_EQ(months[2].moving, 3000u);
}

void test_volume_years()
{
    ActivitySummaries const summaries { run(2009, 6, 1), ride(2010, 5, 1) };
    CHECK((volume_years(summaries, VolumeSport::Running, 2026) == std::pair { 2009, 2026 }));
    CHECK((volume_years(summaries, VolumeSport::Cycling, 2026) == std::pair { 2010, 2026 }));
    CHECK((volume_years({ }, VolumeSport::Cycling, 2026) == std::pair { 2026, 2026 }));
    CHECK_EQ(volume_totals({ }, VolumeSport::Cycling, Period::Year, 2026).size(), 1uz);
}

i64 local_time(int year, unsigned month, unsigned day, int hour, int minute)
{
    std::tm tm {
        .tm_sec = 0,
        .tm_min = minute,
        .tm_hour = hour,
        .tm_mday = static_cast<int>(day),
        .tm_mon = static_cast<int>(month) - 1,
        .tm_year = year - 1900,
        .tm_isdst = -1,
    };
    return static_cast<i64>(std::mktime(&tm));
}

// A week ends at midnight between Sunday and Monday, local time, also in
// the weeks around the changes to and from summer time.
void test_week_boundary_at_midnight()
{
    auto late_sunday = ride(2025, 3, 30);
    late_sunday.start = local_time(2025, 3, 30, 23, 59); // Summer time starts in Europe
    auto early_monday = ride(2025, 3, 31);
    early_monday.start = local_time(2025, 3, 31, 0, 1);
    auto autumn_sunday = ride(2025, 10, 26);
    autumn_sunday.start = local_time(2025, 10, 26, 23, 59);
    auto const weeks = volume_totals({ late_sunday, early_monday, autumn_sunday }, VolumeSport::Cycling, Period::Week, 2025);
    // 6 January is week 0: 24 March is 11, 31 March 12, 20 October 41.
    CHECK(weeks[11].first_day == date(2025, 3, 24));
    CHECK_EQ(weeks[11].count, 1uz);
    CHECK_EQ(weeks[12].count, 1uz);
    CHECK_EQ(weeks[41].count, 1uz);
    CHECK_EQ(weeks[42].count, 0uz);
}

// Other sports, and files that couldn't be read, count nowhere, also not
// in the years the years view starts from.
void test_other_sports_left_out()
{
    ActivitySummary         walk { .start = local_noon(2008, 5, 1), .sport = FIT::sport::Walking, .distance = 5000, .moving = 3600 };
    ActivitySummary         unreadable { .start = local_noon(2007, 5, 1) };
    ActivitySummaries const summaries { unreadable, walk, ride(2010, 5, 1) };
    CHECK((volume_years(summaries, VolumeSport::Cycling, 2026) == std::pair { 2010, 2026 }));
    CHECK((volume_years(summaries, VolumeSport::Running, 2026) == std::pair { 2026, 2026 }));
    auto const months = volume_totals(summaries, VolumeSport::Running, Period::Month, 2008);
    CHECK_EQ(months[4].count, 0uz);
}

// The weeks whose Monday is in the month: March 2025 has five, from the
// 3rd, February 2026 four, from the 2nd.
void test_weeks_of_a_month()
{
    ActivitySummaries const summaries { ride(2025, 3, 2), ride(2025, 3, 3), ride(2025, 4, 6) };
    auto const              march = volume_totals(summaries, VolumeSport::Cycling, Period::Week, 2025, 3u);
    CHECK_EQ(march.size(), 5uz);
    CHECK(march.front().first_day == date(2025, 3, 3));
    CHECK(march.back().first_day == date(2025, 3, 31));
    // Sunday 2 March is in February's last week; Sunday 6 April in the
    // week of Monday 31 March.
    CHECK_EQ(march.front().count, 1uz);
    CHECK_EQ(march.back().count, 1uz);
    auto const february = volume_totals({ }, VolumeSport::Cycling, Period::Week, 2026, 2u);
    CHECK_EQ(february.size(), 4uz);
    CHECK(february.front().first_day == date(2026, 2, 2));
}

void test_year_to_date()
{
    ActivitySummaries const summaries { ride(2025, 1, 1, 40), ride(2025, 1, 3, 60), run(2025, 1, 2), ride(2025, 12, 31, 10) };
    auto const              values = year_to_date(summaries, VolumeSport::Cycling, Metric::Distance, 2025, date(2026, 6, 1));
    CHECK_EQ(values.size(), 365uz);
    CHECK_EQ(values[0], 40000.0);
    CHECK_EQ(values[1], 40000.0); // The run doesn't count
    CHECK_EQ(values[2], 100000.0);
    CHECK_EQ(values[363], 100000.0);
    CHECK_EQ(values[364], 110000.0);
    auto const count = year_to_date(summaries, VolumeSport::Cycling, Metric::Count, 2025, date(2026, 6, 1));
    CHECK_EQ(count.back(), 3.0);
}

// This year's stop at today, a year to come has none, and a leap year
// has 366 days.
void test_year_to_date_until_today()
{
    ActivitySummaries const summaries { ride(2026, 1, 1), ride(2026, 3, 10) };
    auto const              values = year_to_date(summaries, VolumeSport::Cycling, Metric::Distance, 2026, date(2026, 3, 9));
    CHECK_EQ(values.size(), 68uz); // January 31, February 28, 9 of March
    CHECK_EQ(values.back(), 40000.0);
    CHECK(year_to_date(summaries, VolumeSport::Cycling, Metric::Distance, 2027, date(2026, 3, 9)).empty());
    CHECK_EQ(year_to_date({ }, VolumeSport::Cycling, Metric::Distance, 2024, date(2026, 3, 9)).size(), 366uz);
}

void test_year_to_date_months()
{
    ActivitySummaries const summaries { ride(2025, 1, 31, 40), ride(2025, 2, 1, 60), ride(2026, 1, 15, 30) };
    auto const              before = year_to_date(summaries, VolumeSport::Cycling, Metric::Distance, 2025, date(2026, 2, 10));
    auto const              year = year_to_date(summaries, VolumeSport::Cycling, Metric::Distance, 2026, date(2026, 2, 10));
    auto const              rows = year_to_date_months(year, before, 2026);
    CHECK_EQ(rows.size(), 12uz);
    CHECK((rows[0] == YearToDateRow { .month = chrono::January, .year = 30000.0, .before = 40000.0 }));
    // February isn't over: no total at its end yet.
    CHECK((rows[1] == YearToDateRow { .month = chrono::February, .year = { }, .before = 100000.0 }));
    CHECK((rows[11] == YearToDateRow { .month = chrono::December, .year = { }, .before = 100000.0 }));
}

}

int main()
{
    test_volume_sport();
    test_names();
    test_weeks();
    test_week_totals();
    test_week_across_new_year();
    test_months();
    test_years();
    test_without_distance();
    test_volume_years();
    test_week_boundary_at_midnight();
    test_other_sports_left_out();
    test_weeks_of_a_month();
    test_year_to_date();
    test_year_to_date_until_today();
    test_year_to_date_months();
    return Test::test_result();
}
