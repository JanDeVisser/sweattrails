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
[[nodiscard]] std::vector<PeriodTotal> volume_totals(ActivitySummaries const &summaries, VolumeSport sport, Period period, int year);

// The years there are totals of: from the sport's first activity's year to
// `this_year`, or just `this_year` without activities.
[[nodiscard]] std::pair<int, int> volume_years(ActivitySummaries const &summaries, VolumeSport sport, int this_year);

}
