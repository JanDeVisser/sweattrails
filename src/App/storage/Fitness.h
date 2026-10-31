/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Fitness, fatigue and form, as on Coggan and Allen's Performance
 * Management Chart: the chronic and acute training loads (CTL, ATL),
 * averages of the daily training stress over 42 and 7 days, and the
 * training stress balance (TSB) between them.
 */

#pragma once

#include <chrono>
#include <optional>
#include <utility>
#include <vector>

#include <storage/Summaries.h>
#include <storage/Thresholds.h>

namespace ST {

constexpr double ChronicDays = 42;
constexpr double AcuteDays = 7;

struct FitnessDay {
    std::chrono::year_month_day day { };
    float64                     tss { 0 }; // Of the day's activities
    float64                     ctl { 0 }; // At the end of the day
    float64                     atl { 0 };
    float64                     tsb { 0 }; // Going into the day: yesterday's CTL less its ATL
};

// An activity's training stress, with the thresholds of its day.
[[nodiscard]] std::optional<TrainingStress> summary_stress(ActivitySummary const &summary, ThresholdList const &thresholds);

// A day each from the first activity's day to `until`, CTL and ATL
// starting at 0. None before the first activity, or without one.
[[nodiscard]] std::vector<FitnessDay> fitness_days(ActivitySummaries const &summaries, ThresholdList const &thresholds, std::chrono::year_month_day until);

// The day `day` among `days`, if it's there.
[[nodiscard]] std::optional<size_t> fitness_day_at(std::vector<FitnessDay> const &days, std::chrono::year_month_day day);

// How much CTL rose in the week up to `days[ix]`.
[[nodiscard]] float64 ramp_rate(std::vector<FitnessDay> const &days, size_t ix);

// A week whose Monday is in the year: its TSS, and its last day there is,
// Sunday, or today in this week.
struct FitnessWeek {
    std::chrono::year_month_day monday { };
    float64                     tss { 0 };
    FitnessDay                  end { };
};

// The year's weeks that have days among `days`.
[[nodiscard]] std::vector<FitnessWeek> fitness_weeks(std::vector<FitnessDay> const &days, int year);

// The year's activities that have a sport but no training stress: they
// count as rest.
[[nodiscard]] size_t without_stress(ActivitySummaries const &summaries, ThresholdList const &thresholds, int year);

// The years from the first activity's, of any sport, to `this_year`.
[[nodiscard]] std::pair<int, int> fitness_years(ActivitySummaries const &summaries, int this_year);

}
