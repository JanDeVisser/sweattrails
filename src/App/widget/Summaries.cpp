/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <format>

#include <widget/Summaries.h>

namespace ST {

namespace chrono = std::chrono;

namespace {

constexpr std::array MonthNames {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

constexpr std::array WeekDayNames { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

std::string short_month(chrono::month month)
{
    return std::string { MonthNames[static_cast<unsigned>(month) - 1] }.substr(0, 3);
}

// "Mon 3 Mar", "March", "2025".
std::string period_name(PeriodTotal const &total, Period period)
{
    auto const &day = total.first_day;
    switch (period) {
    case Period::Week:
        return std::format("{} {} {}", WeekDayNames[chrono::weekday { chrono::sys_days { day } }.c_encoding()],
            static_cast<unsigned>(day.day()), short_month(day.month()));
    case Period::Month:
        return MonthNames[static_cast<unsigned>(day.month()) - 1];
    case Period::Year:
        return std::format("{}", static_cast<int>(day.year()));
    }
    UNREACHABLE();
}

// Under the bars: a week's only for the first week of a month.
std::string bar_label(PeriodTotal const &total, Period period)
{
    auto const &day = total.first_day;
    switch (period) {
    case Period::Week:
        return static_cast<unsigned>(day.day()) <= 7 ? short_month(day.month()) : "";
    case Period::Month:
        return short_month(day.month());
    case Period::Year:
        return std::format("{}", static_cast<int>(day.year()));
    }
    UNREACHABLE();
}

double bar_value(PeriodTotal const &total, Metric metric)
{
    switch (metric) {
    case Metric::Distance:
        return total.distance / 1000;
    case Metric::Time:
        return static_cast<double>(total.moving) / 3600;
    case Metric::Count:
    case Metric::Ascent:
        return total.value(metric);
    }
    UNREACHABLE();
}

char const *unit(Metric metric)
{
    switch (metric) {
    case Metric::Distance:
        return " km";
    case Metric::Time:
        return " h";
    case Metric::Count:
        return "";
    case Metric::Ascent:
        return " m";
    }
    UNREACHABLE();
}

std::string capitalized(std::string text)
{
    if (!text.empty()) {
        text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    }
    return text;
}

TableRow total_row(std::string name, PeriodTotal const &total)
{
    return TableRow {
        std::move(name),
        std::format("{:.1f} km", total.distance / 1000),
        format_total_time(total.moving),
        std::format("{}", total.count),
        std::format("{} m", total.ascent),
    };
}

}

void SummaryView::set_years(std::pair<int, int> years)
{
    first_year = years.first;
    last_year = years.second;
    year = std::clamp(year, first_year, last_year);
}

bool SummaryView::step_year(int step)
{
    if (period == Period::Year) {
        return false;
    }
    auto const next = std::clamp(year + step, first_year, last_year);
    if (next == year) {
        return false;
    }
    year = next;
    return true;
}

std::string volume_heading(SummaryView const &view)
{
    auto const what = std::format("{} · {} by {}", capitalized(VolumeSport_name(view.sport)), Metric_name(view.metric), Period_name(view.period));
    if (view.period == Period::Year) {
        return what;
    }
    return std::format("{} · {}", what, view.year);
}

std::vector<Bar> volume_bars(std::vector<PeriodTotal> const &totals, Period period, Metric metric)
{
    std::vector<Bar> ret;
    for (auto const &total : totals) {
        ret.push_back(Bar { .label = bar_label(total, period), .value = bar_value(total, metric) });
    }
    return ret;
}

std::vector<TableColumn> volume_columns(Period period, VolumeSport sport)
{
    return {
        { .header = capitalized(Period_name(period)) },
        { .header = "Distance", .right_aligned = true },
        { .header = "Time", .right_aligned = true },
        { .header = sport == VolumeSport::Running ? "Runs" : "Rides", .right_aligned = true },
        { .header = "Ascent", .right_aligned = true },
    };
}

std::vector<TableRow> volume_rows(std::vector<PeriodTotal> const &totals, Period period)
{
    std::vector<TableRow> ret;
    PeriodTotal           all { };
    for (auto const &total : totals) {
        ret.push_back(total_row(period_name(total, period), total));
        all.distance += total.distance;
        all.moving += total.moving;
        all.count += total.count;
        all.ascent += total.ascent;
    }
    ret.push_back(total_row("Total", all));
    return ret;
}

std::string format_total_time(u64 seconds)
{
    return std::format("{}:{:02}", seconds / 3600, (seconds % 3600) / 60);
}

SummariesScreen::SummariesScreen(pWidget const &parent)
    : Layout(parent)
{
}

void SummariesScreen::initialize()
{
    heading = add_widget<Label>("");
    heading->size = FontSize::Large;
    keys = add_widget<Label>("W M Y: week, month, year    R C: running, cycling    D T N A: distance, time, number, ascent    Left Right: year    Esc: back", LIGHTGRAY);
    keys->size = FontSize::Small;
    chart = add_widget<BarChart>();
    chart->on_hover = [this](std::optional<size_t> bar) {
        if (bar) {
            table->select(bar);
        }
    };
    chart->on_click = [this](size_t bar) {
        table->select(bar);
    };
    table = add_widget<Table>(TableRows);
    table->on_click = [this](size_t row) {
        table->select(row);
    };
}

// The chart's highlight follows the table's selection, however it was
// selected. The total row has no bar.
void SummariesScreen::on_draw()
{
    chart->highlight = table->selection;
    if (chart->highlight && *chart->highlight >= chart->bars.size()) {
        chart->highlight.reset();
    }
}

void SummariesScreen::open(VolumeSport sport, int today_year)
{
    this_year = today_year;
    view = SummaryView { .sport = sport, .year = today_year };
    view.set_years(volume_years(summaries, sport, this_year));
    refresh();
}

void SummariesScreen::set_summaries(ActivitySummaries new_summaries)
{
    summaries = std::move(new_summaries);
    view.set_years(volume_years(summaries, view.sport, this_year));
    refresh_in_place();
}

void SummariesScreen::set_period(Period period)
{
    view.period = period;
    refresh();
}

void SummariesScreen::set_sport(VolumeSport sport)
{
    view.sport = sport;
    view.set_years(volume_years(summaries, sport, this_year));
    refresh();
}

void SummariesScreen::set_metric(Metric metric)
{
    view.metric = metric;
    refresh_in_place();
}

void SummariesScreen::step_year(int step)
{
    if (view.step_year(step)) {
        refresh();
    }
}

void SummariesScreen::step_row(int step)
{
    if (table->rows.empty()) {
        return;
    }
    auto const last = table->rows.size() - 1;
    if (!table->selection) {
        table->select(step > 0 ? 0 : last);
        return;
    }
    auto const row = static_cast<long>(*table->selection) + step;
    table->select(static_cast<size_t>(std::clamp(row, 0L, static_cast<long>(last))));
}

// The same periods with new numbers, as after each of the job's chunks:
// the table stays where the user scrolled it, and the selection on its
// period, also if the years view gained a year before it.
void SummariesScreen::refresh_in_place()
{
    auto const                            top_line = table->scroll.top_line;
    std::optional<chrono::year_month_day> selected_period { };
    auto const                            total_selected = table->selection && *table->selection >= totals.size();
    if (table->selection && !total_selected) {
        selected_period = totals[*table->selection].first_day;
    }
    refresh();
    table->scroll.top_line = top_line;
    table->scroll.clamp();
    if (total_selected) {
        table->select(table->rows.size() - 1);
        return;
    }
    if (selected_period) {
        if (auto const period = std::ranges::find(totals, *selected_period, &PeriodTotal::first_day); period != totals.end()) {
            table->select(static_cast<size_t>(period - totals.begin()));
        }
    }
}

void SummariesScreen::refresh()
{
    totals = volume_totals(summaries, view.sport, view.period, view.year);
    heading->text = volume_heading(view);
    chart->set_bars(volume_bars(totals, view.period, view.metric));
    chart->min_step = view.metric == Metric::Count ? 1 : 0;
    chart->format_value = [suffix = unit(view.metric)](double value) {
        return std::format("{:g}{}", value, suffix);
    };
    table->set_rows(volume_columns(view.period, view.sport), volume_rows(totals, view.period));
}

}
