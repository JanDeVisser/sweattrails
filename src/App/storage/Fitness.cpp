/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cmath>

#include <storage/Fitness.h>
#include <storage/Volume.h>

namespace ST {

namespace chrono = std::chrono;

std::optional<TrainingStress> summary_stress(ActivitySummary const &summary, ThresholdList const &thresholds)
{
    auto const np = summary.np.transform([](u16 watts) { return static_cast<float64>(watts); });
    return training_stress(np, summary.moving, summary.hr_load, summary.sport, summary.sub_sport, thresholds_on(thresholds, start_day(summary)));
}

std::vector<FitnessDay> fitness_days(ActivitySummaries const &summaries, ThresholdList const &thresholds, chrono::year_month_day until)
{
    auto const first = std::ranges::find_if(summaries, [](ActivitySummary const &summary) { return summary.sport.has_value(); });
    if (first == summaries.end()) {
        return { };
    }
    auto const from = chrono::sys_days { start_day(*first) };
    auto const to = chrono::sys_days { until };
    if (to < from) {
        return { };
    }
    std::vector<FitnessDay> ret;
    for (auto day = from; day <= to; day += chrono::days { 1 }) {
        ret.push_back(FitnessDay { .day = day });
    }
    for (auto const &summary : summaries) {
        auto const ix = (chrono::sys_days { start_day(summary) } - from).count();
        if (ix < 0 || ix >= std::ssize(ret)) {
            continue;
        }
        if (auto const stress = summary_stress(summary, thresholds); stress) {
            ret[static_cast<size_t>(ix)].tss += stress->value;
        }
    }
    auto const k_ctl = 1 - std::exp(-1 / ChronicDays);
    auto const k_atl = 1 - std::exp(-1 / AcuteDays);
    auto       ctl = 0.0;
    auto       atl = 0.0;
    for (auto &day : ret) {
        day.tsb = ctl - atl;
        ctl += (day.tss - ctl) * k_ctl;
        atl += (day.tss - atl) * k_atl;
        day.ctl = ctl;
        day.atl = atl;
    }
    return ret;
}

std::optional<size_t> fitness_day_at(std::vector<FitnessDay> const &days, chrono::year_month_day day)
{
    if (days.empty()) {
        return { };
    }
    auto const ix = (chrono::sys_days { day } - chrono::sys_days { days.front().day }).count();
    if (ix < 0 || ix >= std::ssize(days)) {
        return { };
    }
    return static_cast<size_t>(ix);
}

float64 ramp_rate(std::vector<FitnessDay> const &days, size_t ix)
{
    if (ix >= days.size()) {
        return 0;
    }
    // Before the first day, CTL was 0.
    auto const week_before = ix >= 7 ? days[ix - 7].ctl : 0.0;
    return days[ix].ctl - week_before;
}

std::vector<FitnessWeek> fitness_weeks(std::vector<FitnessDay> const &days, int year)
{
    std::vector<FitnessWeek> ret;
    chrono::sys_days const   new_year { chrono::year { year } / chrono::January / 1 };
    chrono::sys_days const   new_years_eve { chrono::year { year } / chrono::December / 31 };
    auto                     monday = new_year + (chrono::Monday - chrono::weekday { new_year });
    for (; monday <= new_years_eve; monday += chrono::weeks { 1 }) {
        std::optional<FitnessWeek> week;
        for (auto day = monday; day < monday + chrono::weeks { 1 }; day += chrono::days { 1 }) {
            auto const ix = fitness_day_at(days, day);
            if (!ix) {
                continue;
            }
            if (!week) {
                week = FitnessWeek { .monday = monday };
            }
            week->tss += days[*ix].tss;
            week->end = days[*ix];
        }
        if (week) {
            ret.push_back(*week);
        }
    }
    return ret;
}

size_t without_stress(ActivitySummaries const &summaries, ThresholdList const &thresholds, int year)
{
    return static_cast<size_t>(std::ranges::count_if(summaries, [&](ActivitySummary const &summary) {
        return summary.sport && start_day(summary).year() == chrono::year { year } && !summary_stress(summary, thresholds);
    }));
}

std::pair<int, int> fitness_years(ActivitySummaries const &summaries, int this_year)
{
    auto const first = std::ranges::find_if(summaries, [](ActivitySummary const &summary) { return summary.sport.has_value(); });
    if (first == summaries.end()) {
        return { this_year, this_year };
    }
    return { std::min(static_cast<int>(start_day(*first).year()), this_year), this_year };
}

}
