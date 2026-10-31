/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <functional>
#include <numeric>

#include <Date.h>

#include <storage/Volume.h>

namespace ST {

namespace chrono = std::chrono;

char const *VolumeSport_name(VolumeSport sport)
{
    switch (sport) {
#undef S
#define S(V, N)          \
    case VolumeSport::V: \
        return N;
        VOLUMESPORTS(S)
#undef S
    }
    UNREACHABLE();
}

char const *Period_name(Period period)
{
    switch (period) {
#undef S
#define S(V, N)     \
    case Period::V: \
        return N;
        PERIODS(S)
#undef S
    }
    UNREACHABLE();
}

char const *Metric_name(Metric metric)
{
    switch (metric) {
#undef S
#define S(V, N)     \
    case Metric::V: \
        return N;
        METRICS(S)
#undef S
    }
    UNREACHABLE();
}

std::optional<VolumeSport> VolumeSport_by_name(std::string_view name)
{
#undef S
#define S(V, N)    \
    if (name == N) \
        return VolumeSport::V;
    VOLUMESPORTS(S)
#undef S
    return { };
}

std::optional<Period> Period_by_name(std::string_view name)
{
#undef S
#define S(V, N)    \
    if (name == N) \
        return Period::V;
    PERIODS(S)
#undef S
    return { };
}

std::optional<Metric> Metric_by_name(std::string_view name)
{
#undef S
#define S(V, N)    \
    if (name == N) \
        return Metric::V;
    METRICS(S)
#undef S
    return { };
}

std::optional<VolumeSport> volume_sport(std::optional<FIT::sport> sport, std::optional<FIT::sub_sport> sub_sport)
{
    if (sub_sport) {
        switch (*sub_sport) {
        case FIT::sub_sport::treadmill:
        case FIT::sub_sport::indoor_running:
            return VolumeSport::Running;
        case FIT::sub_sport::spin:
        case FIT::sub_sport::indoor_cycling:
            return VolumeSport::Cycling;
        default:
            break;
        }
    }
    if (sport == FIT::sport::Running) {
        return VolumeSport::Running;
    }
    if (sport == FIT::sport::Cycling) {
        return VolumeSport::Cycling;
    }
    return { };
}

chrono::year_month_day start_day(ActivitySummary const &summary)
{
    return day_of(DateTime::from_timestamp(static_cast<i32>(summary.start)));
}

chrono::year_month_day day_of(DateTime const &time)
{
    return chrono::year { time.year } / chrono::month { static_cast<unsigned>(time.month) + 1 } / chrono::day { time.day_of_month };
}

void PeriodTotal::add(ActivitySummary const &summary)
{
    distance += summary.distance;
    moving += summary.moving;
    ++count;
    ascent += summary.ascent;
}

float64 PeriodTotal::value(Metric metric) const
{
    switch (metric) {
    case Metric::Distance:
        return distance;
    case Metric::Time:
        return static_cast<float64>(moving);
    case Metric::Count:
        return static_cast<float64>(count);
    case Metric::Ascent:
        return static_cast<float64>(ascent);
    }
    UNREACHABLE();
}

namespace {

// The periods, empty, and where in them a day is.
struct Periods {
    std::vector<PeriodTotal>                                   totals;
    std::function<std::optional<size_t>(chrono::sys_days day)> index_of;
};

// The weeks whose Monday is in the year, or in one of its months.
Periods weeks_of(int year, std::optional<unsigned> month)
{
    auto const first_day = chrono::sys_days { chrono::year { year } / chrono::month { month.value_or(1) } / 1 };
    auto const first_monday = first_day + (chrono::Monday - chrono::weekday { first_day });
    auto const in_span = [year, month](chrono::sys_days day) {
        auto const date = chrono::year_month_day { day };
        return date.year() == chrono::year { year } && (!month || date.month() == chrono::month { *month });
    };
    Periods ret;
    for (auto monday = first_monday; in_span(monday); monday += chrono::weeks { 1 }) {
        ret.totals.push_back(PeriodTotal { .first_day = monday, .last_day = monday + chrono::days { 6 } });
    }
    auto const count = ret.totals.size();
    ret.index_of = [first_monday, count](chrono::sys_days day) -> std::optional<size_t> {
        if (day < first_monday) {
            return { };
        }
        auto const week = static_cast<size_t>((day - first_monday).count() / 7);
        if (week >= count) {
            return { };
        }
        return week;
    };
    return ret;
}

Periods months_of(int year)
{
    Periods ret;
    for (auto month = 1u; month <= 12; ++month) {
        auto const first = chrono::year { year } / chrono::month { month } / 1;
        ret.totals.push_back(PeriodTotal { .first_day = first, .last_day = chrono::year_month_day { first.year() / first.month() / chrono::last } });
    }
    ret.index_of = [year](chrono::sys_days day) -> std::optional<size_t> {
        auto const date = chrono::year_month_day { day };
        if (date.year() != chrono::year { year }) {
            return { };
        }
        return static_cast<unsigned>(date.month()) - 1;
    };
    return ret;
}

Periods years_of(int first_year, int last_year)
{
    Periods ret;
    for (auto year = first_year; year <= last_year; ++year) {
        ret.totals.push_back(PeriodTotal {
            .first_day = chrono::year { year } / chrono::January / 1,
            .last_day = chrono::year { year } / chrono::December / 31,
        });
    }
    ret.index_of = [first_year, last_year](chrono::sys_days day) -> std::optional<size_t> {
        auto const year = static_cast<int>(chrono::year_month_day { day }.year());
        if (year < first_year || year > last_year) {
            return { };
        }
        return static_cast<size_t>(year - first_year);
    };
    return ret;
}

}

std::vector<PeriodTotal> volume_totals(ActivitySummaries const &summaries, VolumeSport sport, Period period, int year, std::optional<unsigned> month)
{
    auto periods = [&]() {
        switch (period) {
        case Period::Week:
            return weeks_of(year, month);
        case Period::Month:
            return months_of(year);
        case Period::Year:
            return years_of(volume_years(summaries, sport, year).first, year);
        }
        UNREACHABLE();
    }();
    for (auto const &summary : summaries) {
        if (volume_sport(summary.sport, summary.sub_sport) != sport) {
            continue;
        }
        if (auto const ix = periods.index_of(chrono::sys_days { start_day(summary) }); ix) {
            periods.totals[*ix].add(summary);
        }
    }
    return std::move(periods.totals);
}

std::pair<chrono::year_month, chrono::year_month> volume_months(ActivitySummaries const &summaries, VolumeSport sport, chrono::year_month_day today)
{
    auto const this_month = today.year() / today.month();
    for (auto const &summary : summaries) {
        if (volume_sport(summary.sport, summary.sub_sport) == sport) {
            auto const first = start_day(summary);
            return { std::min(first.year() / first.month(), this_month), this_month };
        }
    }
    return { this_month, this_month };
}

std::pair<int, int> volume_years(ActivitySummaries const &summaries, VolumeSport sport, int this_year)
{
    // The summaries are in order of their start.
    for (auto const &summary : summaries) {
        if (volume_sport(summary.sport, summary.sub_sport) == sport) {
            return { std::min(static_cast<int>(start_day(summary).year()), this_year), this_year };
        }
    }
    return { this_year, this_year };
}

std::vector<double> year_to_date(ActivitySummaries const &summaries, VolumeSport sport, Metric metric, int year, chrono::year_month_day until)
{
    auto const new_year = chrono::sys_days { chrono::year { year } / chrono::January / 1 };
    auto const days = static_cast<size_t>((chrono::sys_days { chrono::year { year + 1 } / chrono::January / 1 } - new_year).count());
    auto       count = days;
    if (until.year() < chrono::year { year }) {
        count = 0;
    } else if (until.year() == chrono::year { year }) {
        count = static_cast<size_t>((chrono::sys_days { until } - new_year).count()) + 1;
    }
    std::vector<double> ret(count, 0.0);
    for (auto const &summary : summaries) {
        if (volume_sport(summary.sport, summary.sub_sport) != sport) {
            continue;
        }
        auto const day = (chrono::sys_days { start_day(summary) } - new_year).count();
        if (day < 0 || static_cast<size_t>(day) >= count) {
            continue;
        }
        PeriodTotal one { };
        one.add(summary);
        ret[static_cast<size_t>(day)] += one.value(metric);
    }
    std::partial_sum(ret.begin(), ret.end(), ret.begin());
    return ret;
}

std::vector<YearToDateRow> year_to_date_months(std::vector<double> const &year_values, std::vector<double> const &before_values, int year)
{
    // The total so far at the end of a day, if the values go that far.
    auto const at = [](std::vector<double> const &values, chrono::year_month_day day) -> std::optional<double> {
        auto const new_year = chrono::sys_days { day.year() / chrono::January / 1 };
        auto const ix = static_cast<size_t>((chrono::sys_days { day } - new_year).count());
        if (ix >= values.size()) {
            return { };
        }
        return values[ix];
    };
    auto const new_year = chrono::sys_days { chrono::year { year } / chrono::January / 1 };
    // The day the year's values go up to, today in this year.
    auto const                 last_day = chrono::year_month_day { new_year + chrono::days { static_cast<int>(year_values.size()) - 1 } };
    std::vector<YearToDateRow> ret;
    for (auto month = 1u; month <= 12; ++month) {
        auto const end = chrono::year { year } / chrono::month { month } / chrono::last;
        auto       row = YearToDateRow { .month = chrono::month { month } };
        if (!year_values.empty() && last_day.year() == chrono::year { year } && last_day.month() == chrono::month { month } && last_day < chrono::year_month_day { end }) {
            // The month so far, against the year before at the same date,
            // or its month's end, for the 29th of February.
            auto before_day = chrono::year { year - 1 } / last_day.month() / last_day.day();
            if (!before_day.ok()) {
                before_day = chrono::year { year - 1 } / last_day.month() / chrono::last;
            }
            row.year = year_values.back();
            row.before = at(before_values, before_day);
            row.until = last_day.day();
        } else {
            row.year = at(year_values, end);
            row.before = at(before_values, chrono::year { year - 1 } / chrono::month { month } / chrono::last);
        }
        ret.push_back(row);
    }
    return ret;
}

}
