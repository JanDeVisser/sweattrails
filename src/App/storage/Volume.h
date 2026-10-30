/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The volume totals: how far and how long the user ran or rode in each
 * week, month or year, from the activities' summaries.
 */

#pragma once

#include <chrono>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <storage/Summaries.h>

namespace ST {

#define VOLUMESPORTS(S)   \
    S(Running, "running") \
    S(Cycling, "cycling")

enum class VolumeSport {
#undef S
#define S(V, N) V,
    VOLUMESPORTS(S)
#undef S
};

#define PERIODS(S)    \
    S(Week, "week")   \
    S(Month, "month") \
    S(Year, "year")

enum class Period {
#undef S
#define S(V, N) V,
    PERIODS(S)
#undef S
};

#define METRICS(S)          \
    S(Distance, "distance") \
    S(Time, "time")         \
    S(Count, "count")       \
    S(Ascent, "ascent")

enum class Metric {
#undef S
#define S(V, N) V,
    METRICS(S)
#undef S
};

char const *VolumeSport_name(VolumeSport sport);
char const *Period_name(Period period);
char const *Metric_name(Metric metric);
// By the names above, as the tasks take them.
std::optional<VolumeSport> VolumeSport_by_name(std::string_view name);
std::optional<Period>      Period_by_name(std::string_view name);
std::optional<Metric>      Metric_by_name(std::string_view name);

// Which total an activity counts in, if any. A treadmill run, or a spin
// bike's ride, counts whatever the sport the device recorded with it.
[[nodiscard]] std::optional<VolumeSport> volume_sport(std::optional<FIT::sport> sport, std::optional<FIT::sub_sport> sub_sport);

// The day an activity starts on, in the computer's time zone, as the store
// files it.
[[nodiscard]] std::chrono::year_month_day start_day(ActivitySummary const &summary);

struct PeriodTotal {
    std::chrono::year_month_day first_day { };
    std::chrono::year_month_day last_day { };
    float64                     distance { 0 }; // m
    u64                         moving { 0 };   // s
    size_t                      count { 0 };
    u64                         ascent { 0 }; // m

    void add(ActivitySummary const &summary);
    // The metric, in metres, seconds or a count.
    [[nodiscard]] float64 value(Metric metric) const;
};

// The sport's totals: of each week whose Monday is in `year`, of each of
// its months, or, by Year, of every year from the sport's first activity
// up to `year`. Periods without activities are there, with nothing in
// them.
// By Week with a `month`, the weeks whose Monday is in that month (1 to
// 12) of the year.
[[nodiscard]] std::vector<PeriodTotal> volume_totals(ActivitySummaries const &summaries, VolumeSport sport, Period period, int year, std::optional<unsigned> month = { });

// The years there are totals of: from the sport's first activity's year to
// `this_year`, or just `this_year` without activities.
[[nodiscard]] std::pair<int, int> volume_years(ActivitySummaries const &summaries, VolumeSport sport, int this_year);

// The sport's total so far of the metric, in metres, seconds or a count,
// at the end of each day of the year, from January 1st: up to and
// including `until`, today, if it's in the year, the whole year if it's
// before, and nothing if it's after.
[[nodiscard]] std::vector<double> year_to_date(ActivitySummaries const &summaries, VolumeSport sport, Metric metric, int year, std::chrono::year_month_day until);

// A month of the year-to-date table: the totals so far at its end, in the
// year and in the year before. None where the values don't go that far,
// as after today.
struct YearToDateRow {
    std::chrono::month    month { 1 };
    std::optional<double> year { };
    std::optional<double> before { };

    bool operator==(YearToDateRow const &) const = default;
};

// From year_to_date() of `year` and of the year before.
[[nodiscard]] std::vector<YearToDateRow> year_to_date_months(std::vector<double> const &year_values, std::vector<double> const &before_values, int year);

}
