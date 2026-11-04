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

// "st", "nd", "rd" or "th", as in "the 15th".
std::string ordinal_suffix(unsigned day)
{
    if (day % 100 >= 11 && day % 100 <= 13) {
        return "th";
    }
    switch (day % 10) {
    case 1:
        return "st";
    case 2:
        return "nd";
    case 3:
        return "rd";
    default:
        return "th";
    }
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

// The months' first days along the lines' x axis, of days from New Year.
std::vector<ChartTick> month_ticks(int year)
{
    auto const             new_year = chrono::sys_days { chrono::year { year } / chrono::January / 1 };
    std::vector<ChartTick> ret;
    for (auto month = 1u; month <= 12; ++month) {
        auto const first = chrono::sys_days { chrono::year { year } / chrono::month { month } / 1 };
        ret.push_back(ChartTick { .x = static_cast<double>((first - new_year).count()), .label = short_month(chrono::month { month }) });
    }
    return ret;
}

int days_in_year(int year)
{
    return chrono::year { year }.is_leap() ? 366 : 365;
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
        auto const first = first_month.ok() ? first_month : chrono::year { first_year } / chrono::January;
        auto const last = last_month.ok() ? last_month : chrono::year { last_year } / chrono::December;
        if (next < first || next > last) {
            return false;
        }
        year = static_cast<int>(next.year());
        month = static_cast<unsigned>(next.month());
        return true;
    }
    if (period == Period::Year && graph == Graph::Bars) {
        return false;
    }
    auto const [first, last] = graph == Graph::Fitness ? fitness_years : std::pair { first_year, last_year };
    auto const next = std::clamp(year + step, first, last);
    if (next == year) {
        return false;
    }
    year = next;
    return true;
}

std::string volume_heading(SummaryView const &view)
{
    auto const sport = capitalized(VolumeSport_name(view.sport));
    if (view.graph == Graph::YearToDate) {
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

std::string fitness_heading(std::vector<FitnessDay> const &days, chrono::year_month_day day, size_t without,
    bool has_thresholds, std::string_view thresholds_error)
{
    // Here as well as on the status line, which the jobs clear at start.
    if (!thresholds_error.empty()) {
        return std::format("The thresholds in settings.json can't be read: {}", thresholds_error);
    }
    if (!has_thresholds) {
        return "Set your FTP and LTHR in settings.json to see your fitness";
    }
    std::string ret { "Fitness" };
    if (auto const ix = fitness_day_at(days, day); ix) {
        auto const &values = days[*ix];
        // To a tenth first: a CTL decaying by less than 0.05 a week would
        // be "-0.0".
        auto ramp = std::round(ramp_rate(days, *ix) * 10) / 10;
        if (ramp == 0) {
            ramp = 0;
        }
        ret += std::format(" · CTL {} · ATL {} · TSB {} · {:+.1f} a week", std::llround(values.ctl), std::llround(values.atl),
            std::llround(values.tsb), ramp);
    }
    ret += std::format(" · {}", static_cast<int>(day.year()));
    if (without > 0) {
        ret += std::format(" · {} without TSS", without);
    }
    return ret;
}

std::vector<TableColumn> fitness_columns(bool has_thresholds)
{
    if (!has_thresholds) {
        return { { .header = "In settings.json, for example" } };
    }
    return {
        { .header = "Week" },
        { .header = "TSS", .right_aligned = true },
        { .header = "CTL", .right_aligned = true },
        { .header = "ATL", .right_aligned = true },
        { .header = "TSB", .right_aligned = true },
    };
}

std::vector<TableRow> fitness_rows(std::vector<FitnessWeek> const &weeks, bool has_thresholds)
{
    if (!has_thresholds) {
        return {
            { R"("thresholds": [)" },
            { R"(    { "from": "2020-01-01", "ftp": 250, "lthr_running": 170, "lthr_cycling": 160 },)" },
            { R"(    { "from": "2024-03-01", "ftp": 275 })" },
            { "]" },
        };
    }
    std::vector<TableRow> ret;
    for (auto const &week : weeks) {
        ret.push_back(TableRow {
            period_name(PeriodTotal { .first_day = week.monday }, Period::Week),
            std::format("{}", std::llround(week.tss)),
            std::format("{}", std::llround(week.end.ctl)),
            std::format("{}", std::llround(week.end.atl)),
            std::format("{}", std::llround(week.end.tsb)),
        });
    }
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
        auto       name = std::string { MonthNames[static_cast<unsigned>(row.month) - 1] };
        if (row.until) {
            name = std::format("{} to the {}{}", name, static_cast<unsigned>(*row.until), ordinal_suffix(static_cast<unsigned>(*row.until)));
        }
        ret.push_back(TableRow { std::move(name), format(row.year), format(row.before), difference });
    }
    return ret;
}

std::string format_total_time(u64 seconds)
{
    return std::format("{}:{:02}", seconds / 3600, (seconds % 3600) / 60);
}

namespace {

// "12 Oct 2026".
std::string format_day(chrono::year_month_day day)
{
    return std::format("{} {} {}", static_cast<unsigned>(day.day()), short_month(day.month()), static_cast<int>(day.year()));
}

}

std::string power_heading(std::vector<PowerBest> const &window, std::optional<CriticalPower> const &cp, std::optional<float64> ftp,
    chrono::year_month_day end)
{
    std::string ret { "Power" };
    if (window.empty()) {
        ret += " · no rides with power";
    } else {
        if (cp) {
            ret += std::format(" · CP {} W · W' {} kJ", std::llround(cp->cp), std::llround(cp->w_prime / 1000));
        }
        if (ftp) {
            ret += std::format(" · FTP {} W", std::llround(*ftp));
        }
    }
    return ret + std::format(" · {} days to {}", CurveWindowDays, format_day(end));
}

std::vector<PowerRow> power_table(std::vector<PowerBest> const &bests)
{
    std::vector<PowerRow> ret;
    for (auto const seconds : PowerTableDurations) {
        auto const ix = static_cast<size_t>(std::ranges::find(PowerDurations, seconds) - PowerDurations.begin());
        if (ix < bests.size()) {
            ret.push_back(PowerRow { .seconds = seconds, .best = bests[ix] });
        }
    }
    return ret;
}

std::string format_duration(u32 seconds)
{
    if (seconds < 60) {
        return std::format("{} s", seconds);
    }
    if (seconds < 3600) {
        return seconds % 60 == 0 ? std::format("{} min", seconds / 60) : std::format("{}:{:02} min", seconds / 60, seconds % 60);
    }
    return seconds % 3600 == 0 ? std::format("{} h", seconds / 3600) : std::format("{}:{:02} h", seconds / 3600, (seconds % 3600) / 60);
}

std::vector<TableColumn> power_columns()
{
    return {
        { .header = "Time" },
        { .header = "Power", .right_aligned = true },
        { .header = "Date" },
        { .header = "Ride" },
    };
}

std::vector<TableRow> power_rows(std::vector<PowerRow> const &rows, ActivitySummaries const &summaries,
    std::function<std::string(ActivitySummary const &)> const &name_of)
{
    std::vector<TableRow> ret;
    for (auto const &row : rows) {
        auto const &ride = summaries[row.best.summary];
        ret.push_back(TableRow {
            format_duration(row.seconds),
            std::format("{} W", row.best.watts),
            format_day(start_day(ride)),
            name_of ? name_of(ride) : std::string { },
        });
    }
    return ret;
}

namespace {

// The `values` at the `xs` they're of, sampled at CurvePoints from
// `first` to `last`, evenly in the logarithm of x, and interpolated in it
// between the xs: up to the last of the values, so a shorter curve stops
// partway.
std::vector<double> log_line(std::span<u32 const> xs, std::vector<double> const &values, u32 first, u32 last)
{
    if (values.empty() || last < first) {
        return { };
    }
    // A single value, or an axis of one x: a dot.
    if (values.size() == 1 || last == first) {
        return { values[0] };
    }
    auto const          log_first = std::log(static_cast<double>(first));
    auto const          log_last = std::log(static_cast<double>(last));
    auto const          end = static_cast<double>(xs[values.size() - 1]);
    std::vector<double> ret;
    for (auto ix = 0uz; ix < CurvePoints; ++ix) {
        auto const at = log_first + (log_last - log_first) * static_cast<double>(ix) / static_cast<double>(CurvePoints - 1);
        if (std::exp(at) > end * (1 + 1e-9)) {
            break;
        }
        auto upper = 1uz;
        while (upper < values.size() - 1 && std::log(static_cast<double>(xs[upper])) < at) {
            ++upper;
        }
        auto const from = std::log(static_cast<double>(xs[upper - 1]));
        auto const to = std::log(static_cast<double>(xs[upper]));
        auto const fraction = std::clamp((at - from) / (to - from), 0.0, 1.0);
        ret.push_back(values[upper - 1] + fraction * (values[upper] - values[upper - 1]));
    }
    return ret;
}

struct LogTick {
    u32         x { 0 };
    std::string label { };
};

// The ticks from `first` to `last`, on the axis log_line() samples.
std::vector<ChartTick> log_ticks(std::span<LogTick const> ticks, u32 first, u32 last)
{
    std::vector<ChartTick> ret;
    if (last <= first) {
        return ret;
    }
    auto const span = std::log(static_cast<double>(last)) - std::log(static_cast<double>(first));
    for (auto const &tick : ticks) {
        if (tick.x < first || tick.x > last) {
            continue;
        }
        ret.push_back(ChartTick {
            .x = (std::log(static_cast<double>(tick.x)) - std::log(static_cast<double>(first))) / span * static_cast<double>(CurvePoints - 1),
            .label = tick.label,
        });
    }
    return ret;
}

}

std::vector<double> curve_line(std::vector<PowerBest> const &bests, u32 longest)
{
    std::vector<double> watts;
    for (auto const &best : bests) {
        watts.push_back(best.watts);
    }
    return log_line(PowerDurations, watts, 1, longest);
}

std::vector<ChartTick> curve_ticks(u32 longest)
{
    std::vector<LogTick> ticks;
    for (auto const seconds : { 1u, 10u, 60u, 300u, 1200u, 3600u, 7200u, 18000u, 36000u }) {
        ticks.push_back(LogTick { .x = seconds, .label = format_duration(seconds) });
    }
    return log_ticks(ticks, 1, longest);
}

namespace {

// "4:19", of seconds; "1:32:10" from an hour.
std::string format_time(double seconds)
{
    auto const whole = std::llround(seconds);
    if (whole >= 3600) {
        return std::format("{}:{:02}:{:02}", whole / 3600, (whole % 3600) / 60, whole % 60);
    }
    return std::format("{}:{:02}", whole / 60, whole % 60);
}

}

std::string pace_heading(std::vector<PaceBest> const &window, std::optional<CriticalSpeed> const &cs, chrono::year_month_day end)
{
    std::string ret { "Pace" };
    if (window.empty()) {
        ret += " · no runs";
    } else if (cs) {
        ret += std::format(" · CS {} /km · D' {} m", format_time(1000 / cs->cs), std::llround(cs->d_prime));
    }
    return ret + std::format(" · {} days to {}", CurveWindowDays, format_day(end));
}

std::vector<PaceRow> pace_table(std::vector<PaceBest> const &bests)
{
    std::vector<PaceRow> ret;
    for (auto const metres : PaceTableDistances) {
        auto const ix = static_cast<size_t>(std::ranges::find(PaceDistances, metres) - PaceDistances.begin());
        if (ix < bests.size()) {
            ret.push_back(PaceRow { .metres = metres, .best = bests[ix] });
        }
    }
    return ret;
}

std::string format_race_distance(u32 metres)
{
    switch (metres) {
    case 1609:
        return "1 mile";
    case 16093:
        return "10 miles";
    case 21097:
        return "Half marathon";
    case 42195:
        return "Marathon";
    default:
        break;
    }
    if (metres < 1000 || metres % 1000 != 0) {
        return std::format("{} m", metres);
    }
    return std::format("{} km", metres / 1000);
}

std::vector<TableColumn> pace_columns()
{
    return {
        { .header = "Distance" },
        { .header = "Time", .right_aligned = true },
        { .header = "Pace", .right_aligned = true },
        { .header = "Date" },
        { .header = "Run" },
    };
}

std::vector<TableRow> pace_rows(std::vector<PaceRow> const &rows, ActivitySummaries const &summaries,
    std::function<std::string(ActivitySummary const &)> const &name_of)
{
    std::vector<TableRow> ret;
    for (auto const &row : rows) {
        auto const &run = summaries[row.best.summary];
        ret.push_back(TableRow {
            format_race_distance(row.metres),
            format_time(row.best.seconds),
            std::format("{} /km", format_time(row.best.seconds * 1000.0 / row.metres)),
            format_day(start_day(run)),
            name_of ? name_of(run) : std::string { },
        });
    }
    return ret;
}

std::vector<double> pace_line(std::vector<PaceBest> const &bests, u32 longest)
{
    std::vector<double> paces;
    for (auto ix = 0uz; ix < bests.size(); ++ix) {
        paces.push_back(bests[ix].seconds * 1000.0 / PaceDistances[ix]);
    }
    return log_line(PaceDistances, paces, PaceDistances[0], longest);
}

std::vector<ChartTick> pace_ticks(u32 longest)
{
    std::vector<LogTick> ticks;
    for (auto const metres : { 400u, 1000u, 5000u, 10000u, 21097u, 42195u }) {
        ticks.push_back(LogTick { .x = metres, .label = metres == 21097 ? "Half" : format_race_distance(metres) });
    }
    return log_ticks(ticks, PaceDistances[0], longest);
}

std::string summaries_keys(Graph graph)
{
    return std::format("W M Y: week, month, year    G: to date    F: fitness    P: best power or pace    R C: running, cycling    "
                       "D T N A: distance, time, number, ascent    Left Right: {}    Enter: open    Backspace: back    Esc: close",
        graph == Graph::Curve ? std::format("{} weeks", CurveStepDays / 7) : "year");
}

SummariesScreen::SummariesScreen(pWidget const &parent)
    : Layout(parent)
{
}

void SummariesScreen::initialize()
{
    heading = add_widget<Label>("");
    heading->size = FontSize::Large;
    keys = add_widget<Label>(summaries_keys(Graph::Bars), LIGHTGRAY);
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
    ride_names.clear();
    view = SummaryView { .sport = sport, .year = static_cast<int>(today.year()) };
    set_span();
    refresh();
}

void SummariesScreen::set_summaries(ActivitySummaries new_summaries)
{
    summaries = std::move(new_summaries);
    set_span();
    refresh_in_place();
}

void SummariesScreen::set_period(Period period)
{
    trail.clear();
    view.period = period;
    view.month.reset();
    show_graph(Graph::Bars);
}

void SummariesScreen::set_sport(VolumeSport sport)
{
    view.sport = sport;
    set_span();
    refresh();
}

void SummariesScreen::set_metric(Metric metric)
{
    view.metric = metric;
    refresh_in_place();
}

void SummariesScreen::toggle_lines()
{
    show_graph(view.graph == Graph::YearToDate ? Graph::Bars : Graph::YearToDate);
}

void SummariesScreen::toggle_fitness()
{
    show_graph(view.graph == Graph::Fitness ? Graph::Bars : Graph::Fitness);
}

void SummariesScreen::toggle_curve()
{
    view.curve_end = today;
    show_graph(view.graph == Graph::Curve ? Graph::Bars : Graph::Curve);
}

// At the top. Leaving the fitness, whose years are those of any sport,
// the year goes back among the sport's.
void SummariesScreen::show_graph(Graph graph)
{
    trail.clear();
    view.month.reset();
    view.graph = graph;
    set_span();
    refresh();
}

void SummariesScreen::step_year(int step)
{
    if (view.graph == Graph::Curve ? step_curve(step) : view.step_year(step)) {
        refresh();
    }
}

// Forward up to today, and back while the window ends on or after the
// first ride with power, or outdoor run with best times.
bool SummariesScreen::step_curve(int step)
{
    auto const end = chrono::sys_days { view.curve_end } + chrono::days { step * CurveStepDays };
    auto const first = std::ranges::find_if(summaries, [this](ActivitySummary const &summary) {
        if (view.sport == VolumeSport::Running) {
            return !summary.best_times.empty() && outdoor_run(summary);
        }
        return !summary.power.empty() && volume_sport(summary.sport, summary.sub_sport) == VolumeSport::Cycling;
    });
    if (end > chrono::sys_days { today } || first == summaries.end() || end < chrono::sys_days { start_day(*first) }) {
        return false;
    }
    view.curve_end = end;
    return true;
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
    if (view.graph == Graph::Curve) {
        if (!bar && ix && *ix < curve_rides.size() && on_ride) {
            on_ride(summaries[curve_rides[*ix]]);
        }
        return;
    }
    if (view.graph != Graph::Bars || !ix || *ix >= totals.size()) {
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
    // The period on display, which Left, Right, R or C may have moved
    // since drilling into it: back up, it's selected, in its own year.
    auto const drilled_into = chrono::year { view.year } / chrono::month { view.month.value_or(1) } / 1;
    view.period = trail.back().period;
    view.month = trail.back().month;
    trail.pop_back();
    refresh();
    select_period(drilled_into);
    return true;
}

void SummariesScreen::set_span()
{
    view.fitness_years = fitness_years(summaries, static_cast<int>(today.year()));
    if (view.graph == Graph::Fitness) {
        // R and C change the view underneath, not the year shown.
        auto const year = view.year;
        view.set_years(volume_years(summaries, view.sport, static_cast<int>(today.year())));
        view.year = std::clamp(year, view.fitness_years.first, view.fitness_years.second);
    } else {
        view.set_years(volume_years(summaries, view.sport, static_cast<int>(today.year())));
    }
    std::tie(view.first_month, view.last_month) = volume_months(summaries, view.sport, today);
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
    auto const                            bars = view.graph == Graph::Bars;
    auto const                            total_selected = bars && selection && *selection >= totals.size();
    if (bars && selection && !total_selected) {
        selected_period = totals[*selection].first_day;
    }
    refresh();
    table->scroll.top_line = top_line;
    table->scroll.clamp();
    if (!bars) {
        // The rows are the months or the weeks, whatever the numbers.
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
    keys->text = summaries_keys(view.graph);
    auto const suffix = unit(view.metric);
    auto const format_value = [suffix](double value) {
        return std::format("{:g}{}", value, suffix);
    };
    auto const min_step = view.metric == Metric::Count ? 1.0 : 0.0;
    auto const policy = [](bool shown) { return shown ? SizePolicy::Stretch : SizePolicy::Hide; };
    auto const bars = view.graph == Graph::Bars;
    if (chart->policy != policy(bars)) {
        chart->policy = policy(bars);
        lines->policy = policy(!bars);
        resize();
    }
    if (!bars) {
        totals.clear();
        chart->set_bars({ });
    }
    if (view.graph != Graph::Curve) {
        curve_rides.clear();
    }
    lines->steps.clear();
    if (view.graph == Graph::Fitness || view.graph == Graph::Curve) {
        lines->format_value = [](double value) { return std::format("{:g}", value); };
        lines->min_step = 0;
        if (view.graph == Graph::Fitness) {
            refresh_fitness();
        } else if (view.sport == VolumeSport::Running) {
            lines->format_value = [](double value) { return format_time(value); };
            lines->steps = { PaceSteps.begin(), PaceSteps.end() };
            refresh_pace();
        } else {
            refresh_power();
        }
        return;
    }
    if (view.graph == Graph::YearToDate) {
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
    // None before the sport's first year, which has no line either: its
    // column is empty, not zeros.
    auto const before_values = view.year - 1 < view.first_year
        ? std::vector<double> { }
        : year_to_date(summaries, view.sport, view.metric, view.year - 1, today);
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
    lines->x_count = static_cast<size_t>(days_in_year(view.year));
    for (auto const &line : lines->series) {
        lines->x_count = std::max(lines->x_count, line.values.size());
    }
    lines->ticks = month_ticks(view.year);
    table->set_rows(year_to_date_columns(view.year), year_to_date_rows(year_to_date_months(year_values, before_values, view.year), view.metric));
}

// CTL, ATL and TSB through the year, up to today in this one; a week a
// row in the table.
void SummariesScreen::refresh_fitness()
{
    auto const has_thresholds = !thresholds.empty();
    auto const new_years_eve = chrono::year { view.year } / chrono::December / 31;
    auto const until = std::min(chrono::sys_days { today }, chrono::sys_days { new_years_eve });
    // The year's last week runs to its Sunday, in the new year, as on the
    // bars: the chart and the heading stop at New Year's Eve.
    auto const last_monday = chrono::sys_days { new_years_eve } - (chrono::weekday { chrono::sys_days { new_years_eve } } - chrono::Monday);
    auto const last_week_until = std::min(chrono::sys_days { today }, last_monday + chrono::days { 6 });
    auto const days = has_thresholds ? fitness_days(summaries, thresholds, last_week_until) : std::vector<FitnessDay> { };
    heading->text = fitness_heading(days, until, without_stress(summaries, thresholds, view.year), has_thresholds, thresholds_error);
    lines->series = {
        ChartSeries { .label = "CTL", .color = SKYBLUE, .bold = true },
        ChartSeries { .label = "ATL", .color = Fade(PINK, 0.7f) },
        ChartSeries { .label = "TSB", .color = Fade(YELLOW, 0.7f) },
    };
    if (!days.empty()) {
        // The days of the year before the first activity, at 0.
        for (auto day = chrono::sys_days { chrono::year { view.year } / chrono::January / 1 }; day <= until; day += chrono::days { 1 }) {
            auto const ix = fitness_day_at(days, day);
            auto const values = ix ? days[*ix] : FitnessDay { };
            lines->series[0].values.push_back(values.ctl);
            lines->series[1].values.push_back(values.atl);
            lines->series[2].values.push_back(values.tsb);
        }
    }
    if (lines->series[0].values.empty()) {
        lines->series.clear();
    }
    lines->x_count = static_cast<size_t>(days_in_year(view.year));
    lines->ticks = month_ticks(view.year);
    table->set_rows(fitness_columns(has_thresholds), fitness_rows(fitness_weeks(days, view.year), has_thresholds));
}

}

namespace ST {

// The window's curve, bold, over the all-time best, faint; the window's
// bests in the table.
void SummariesScreen::refresh_power()
{
    auto const first = chrono::sys_days { view.curve_end } - chrono::days { CurveWindowDays - 1 };
    auto const window = best_curve(summaries, first, view.curve_end);
    auto const all_time = best_curve(summaries, chrono::year::min() / chrono::January / 1, today);
    auto const thresholds_then = thresholds_on(thresholds, view.curve_end);
    heading->text = power_heading(window, critical_power(window), thresholds_then ? thresholds_then->ftp : std::nullopt, view.curve_end);
    lines->series.clear();
    auto const longest = all_time.empty() ? 0u : PowerDurations[all_time.size() - 1];
    if (!all_time.empty()) {
        lines->series.push_back(ChartSeries { .label = "All time", .color = Fade(LIGHTGRAY, 0.5f), .values = curve_line(all_time, longest) });
    }
    if (!window.empty()) {
        lines->series.push_back(ChartSeries {
            .label = std::format("{} days", CurveWindowDays), .color = GOLD, .bold = true, .values = curve_line(window, longest) });
    }
    lines->x_count = CurvePoints;
    lines->ticks = curve_ticks(longest);
    auto const rows = power_table(window);
    curve_rides.clear();
    for (auto const &row : rows) {
        curve_rides.push_back(row.best.summary);
    }
    table->set_rows(power_columns(), power_rows(rows, summaries, [this](ActivitySummary const &ride) { return name_of(ride); }));
}

std::string const &SummariesScreen::name_of(ActivitySummary const &summary)
{
    auto [name, added] = ride_names.try_emplace(summary.source.file);
    if (added && ride_name) {
        name->second = ride_name(summary);
    }
    return name->second;
}

}

namespace ST {

// As the power curve: the window's paces, bold, over the all-time best.
void SummariesScreen::refresh_pace()
{
    auto const first = chrono::sys_days { view.curve_end } - chrono::days { CurveWindowDays - 1 };
    auto const window = best_pace_curve(summaries, first, view.curve_end);
    auto const all_time = best_pace_curve(summaries, chrono::year::min() / chrono::January / 1, today);
    heading->text = pace_heading(window, critical_speed(window), view.curve_end);
    lines->series.clear();
    auto const longest = all_time.empty() ? 0u : PaceDistances[all_time.size() - 1];
    if (!all_time.empty()) {
        lines->series.push_back(ChartSeries { .label = "All time", .color = Fade(LIGHTGRAY, 0.5f), .values = pace_line(all_time, longest) });
    }
    if (!window.empty()) {
        lines->series.push_back(ChartSeries {
            .label = std::format("{} days", CurveWindowDays), .color = GOLD, .bold = true, .values = pace_line(window, longest) });
    }
    lines->x_count = CurvePoints;
    lines->ticks = pace_ticks(longest);
    auto const rows = pace_table(window);
    curve_rides.clear();
    for (auto const &row : rows) {
        curve_rides.push_back(row.best.summary);
    }
    table->set_rows(pace_columns(), pace_rows(rows, summaries, [this](ActivitySummary const &run) { return name_of(run); }));
}

}
