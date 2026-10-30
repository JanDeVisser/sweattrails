/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <functional>

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
    auto const start = DateTime::from_timestamp(static_cast<i32>(summary.start));
    return chrono::year { start.year } / chrono::month { static_cast<unsigned>(start.month) + 1 } / chrono::day { start.day_of_month };
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

Periods weeks_of(int year)
{
    auto const new_year = chrono::sys_days { chrono::year { year } / chrono::January / 1 };
    auto const first_monday = new_year + (chrono::Monday - chrono::weekday { new_year });
    Periods    ret;
    for (auto monday = first_monday; chrono::year_month_day { monday }.year() == chrono::year { year }; monday += chrono::weeks { 1 }) {
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

std::vector<PeriodTotal> volume_totals(ActivitySummaries const &summaries, VolumeSport sport, Period period, int year)
{
    auto periods = [&]() {
        switch (period) {
        case Period::Week:
            return weeks_of(year);
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

}
