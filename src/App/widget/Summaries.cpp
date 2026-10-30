/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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

// A metric's metres, seconds or count in the unit the scale shows.
double display_value(double value, Metric metric)
{
    switch (metric) {
    case Metric::Distance:
        return value / 1000;
    case Metric::Time:
        return value / 3600;
    case Metric::Count:
    case Metric::Ascent:
        return value;
    }
    UNREACHABLE();
}

// A total so far in the year-to-date table, or, signed, the difference
// between two.
std::string format_metric(double value, Metric metric, bool sign = false)
{
    auto const prefix = !sign ? "" : value < 0 ? "-"
                                               : "+";
    auto const size = std::abs(value);
    switch (metric) {
    case Metric::Distance:
        return std::format("{}{:.1f} km", prefix, size / 1000);
    case Metric::Time:
        return std::format("{}{}", prefix, format_total_time(static_cast<u64>(std::llround(size))));
    case Metric::Count:
        return std::format("{}{:.0f}", prefix, size);
    case Metric::Ascent:
        return std::format("{}{:.0f} m", prefix, size);
    }
    UNREACHABLE();
}

// The lines' colours: the year shown, and the years before it, fainter.
Color line_color(int years_back)
{
    switch (years_back) {
    case 0:
        return SKYBLUE;
    case 1:
        return Fade(ORANGE, 0.7f);
    default:
        return Fade(LIME, 0.6f);
    }
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
    if (month) {
        auto const next = chrono::year_month { chrono::year { year }, chrono::month { *month } } + chrono::months { step };
        if (next.year() < chrono::year { first_year } || next.year() > chrono::year { last_year }) {
            return false;
        }
        year = static_cast<int>(next.year());
        month = static_cast<unsigned>(next.month());
        return true;
    }
    if (period == Period::Year && !lines) {
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
    auto const sport = capitalized(VolumeSport_name(view.sport));
    if (view.lines) {
        return std::format("{} · {} to date · {}", sport, Metric_name(view.metric), view.year);
    }
    if (view.month) {
        return std::format("{} · {} by week · {} {}", sport, Metric_name(view.metric), MonthNames[*view.month - 1], view.year);
    }
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
        ret.push_back(Bar { .label = bar_label(total, period), .value = display_value(total.value(metric), metric) });
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

std::vector<TableColumn> year_to_date_columns(int year)
{
    return {
        { .header = "Month" },
        { .header = std::format("{}", year), .right_aligned = true },
        { .header = std::format("{}", year - 1), .right_aligned = true },
        { .header = "Difference", .right_aligned = true },
    };
}

std::vector<TableRow> year_to_date_rows(std::vector<YearToDateRow> const &rows, Metric metric)
{
    std::vector<TableRow> ret;
    for (auto const &row : rows) {
        auto const format = [metric](std::optional<double> value) {
            return value ? format_metric(*value, metric) : std::string { };
        };
        auto const difference = (row.year && row.before) ? format_metric(*row.year - *row.before, metric, true) : std::string { };
        ret.push_back(TableRow { MonthNames[static_cast<unsigned>(row.month) - 1], format(row.year), format(row.before), difference });
    }
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
    keys = add_widget<Label>("W M Y: week, month, year    G: to date    R C: running, cycling    D T N A: distance, time, number, ascent    "
                             "Left Right: year    Enter: open    Backspace: back    Esc: close",
        LIGHTGRAY);
    keys->size = FontSize::Small;
    chart = add_widget<BarChart>();
    chart->on_hover = [this](std::optional<size_t> bar) {
        if (bar) {
            table->select(bar);
        }
    };
    chart->on_click = [this](size_t bar) {
        drill(bar);
    };
    lines = add_widget<LineChart>();
    lines->policy = SizePolicy::Hide;
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

void SummariesScreen::open(VolumeSport sport, chrono::year_month_day today_date)
{
    today = today_date;
    trail.clear();
    view = SummaryView { .sport = sport, .year = static_cast<int>(today.year()) };
    view.set_years(volume_years(summaries, sport, view.year));
    refresh();
}

void SummariesScreen::set_summaries(ActivitySummaries new_summaries)
{
    summaries = std::move(new_summaries);
    view.set_years(volume_years(summaries, view.sport, static_cast<int>(today.year())));
    refresh_in_place();
}

void SummariesScreen::set_period(Period period)
{
    trail.clear();
    view.period = period;
    view.month.reset();
    view.lines = false;
    refresh();
}

void SummariesScreen::set_sport(VolumeSport sport)
{
    view.sport = sport;
    view.set_years(volume_years(summaries, sport, static_cast<int>(today.year())));
    refresh();
}

void SummariesScreen::set_metric(Metric metric)
{
    view.metric = metric;
    refresh_in_place();
}

void SummariesScreen::toggle_lines()
{
    trail.clear();
    view.month.reset();
    view.lines = !view.lines;
    refresh();
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

void SummariesScreen::drill(std::optional<size_t> bar)
{
    auto const ix = bar ? bar : table->selection;
    if (view.lines || !ix || *ix >= totals.size()) {
        return;
    }
    auto const &period = totals[*ix];
    switch (view.period) {
    case Period::Year:
        trail.push_back(view);
        view.period = Period::Month;
        view.year = static_cast<int>(period.first_day.year());
        break;
    case Period::Month:
        trail.push_back(view);
        view.period = Period::Week;
        view.year = static_cast<int>(period.first_day.year());
        view.month = static_cast<unsigned>(period.first_day.month());
        break;
    case Period::Week: {
        auto const first = std::ranges::find_if(summaries, [this, &period](ActivitySummary const &summary) {
            auto const day = start_day(summary);
            return volume_sport(summary.sport, summary.sub_sport) == view.sport && day >= period.first_day && day <= period.last_day;
        });
        if (first != summaries.end() && on_week) {
            on_week(*first);
        }
        return;
    }
    }
    refresh();
}

bool SummariesScreen::up()
{
    if (trail.empty()) {
        return false;
    }
    // The period the user drilled into: back up, it's selected.
    auto const drilled_into = chrono::year { view.year } / chrono::month { view.month.value_or(1) } / 1;
    view.period = trail.back().period;
    view.year = trail.back().year;
    view.month = trail.back().month;
    trail.pop_back();
    refresh();
    select_period(drilled_into);
    return true;
}

void SummariesScreen::select_period(chrono::year_month_day first_day)
{
    if (auto const period = std::ranges::find(totals, first_day, &PeriodTotal::first_day); period != totals.end()) {
        table->select(static_cast<size_t>(period - totals.begin()));
    }
}

// The same periods with new numbers, as after each of the job's chunks:
// the table stays where the user scrolled it, and the selection on its
// period, also if the years view gained a year before it.
void SummariesScreen::refresh_in_place()
{
    auto const                            top_line = table->scroll.top_line;
    auto const                            selection = table->selection;
    std::optional<chrono::year_month_day> selected_period { };
    auto const                            total_selected = !view.lines && selection && *selection >= totals.size();
    if (!view.lines && selection && !total_selected) {
        selected_period = totals[*selection].first_day;
    }
    refresh();
    table->scroll.top_line = top_line;
    table->scroll.clamp();
    if (view.lines) {
        // The rows are the months, whatever the numbers.
        table->select(selection);
    } else if (total_selected) {
        table->select(table->rows.size() - 1);
    } else if (selected_period) {
        select_period(*selected_period);
    }
}

void SummariesScreen::refresh()
{
    heading->text = volume_heading(view);
    auto const suffix = unit(view.metric);
    auto const format_value = [suffix](double value) {
        return std::format("{:g}{}", value, suffix);
    };
    auto const min_step = view.metric == Metric::Count ? 1.0 : 0.0;
    auto const policy = [](bool shown) { return shown ? SizePolicy::Stretch : SizePolicy::Hide; };
    if (chart->policy != policy(!view.lines)) {
        chart->policy = policy(!view.lines);
        lines->policy = policy(view.lines);
        resize();
    }
    if (view.lines) {
        totals.clear();
        chart->set_bars({ });
        lines->format_value = format_value;
        lines->min_step = min_step;
        refresh_lines();
        return;
    }
    totals = volume_totals(summaries, view.sport, view.period, view.year, view.month);
    chart->set_bars(volume_bars(totals, view.period, view.metric));
    chart->min_step = min_step;
    chart->format_value = format_value;
    table->set_rows(volume_columns(view.period, view.sport), volume_rows(totals, view.period));
}

// A line for the year and each of the two before it that has totals, and
// a month's ticks along the bottom.
void SummariesScreen::refresh_lines()
{
    auto const year_values = year_to_date(summaries, view.sport, view.metric, view.year, today);
    auto const before_values = year_to_date(summaries, view.sport, view.metric, view.year - 1, today);
    lines->series.clear();
    for (auto back = 0; back <= ComparedYears; ++back) {
        auto const year = view.year - back;
        if (back > 0 && year < view.first_year) {
            break;
        }
        auto values = back == 0 ? year_values : back == 1 ? before_values
                                                          : year_to_date(summaries, view.sport, view.metric, year, today);
        for (auto &value : values) {
            value = display_value(value, view.metric);
        }
        lines->series.push_back(ChartSeries { .label = std::format("{}", year), .color = line_color(back), .bold = back == 0, .values = std::move(values) });
    }
    auto const new_year = chrono::sys_days { chrono::year { view.year } / chrono::January / 1 };
    lines->x_count = static_cast<size_t>((chrono::sys_days { chrono::year { view.year + 1 } / chrono::January / 1 } - new_year).count());
    for (auto const &line : lines->series) {
        lines->x_count = std::max(lines->x_count, line.values.size());
    }
    lines->ticks.clear();
    for (auto month = 1u; month <= 12; ++month) {
        auto const first = chrono::sys_days { chrono::year { view.year } / chrono::month { month } / 1 };
        lines->ticks.push_back(ChartTick { .x = static_cast<double>((first - new_year).count()), .label = short_month(chrono::month { month }) });
    }
    table->set_rows(year_to_date_columns(view.year), year_to_date_rows(year_to_date_months(year_values, before_values, view.year), view.metric));
}

}
