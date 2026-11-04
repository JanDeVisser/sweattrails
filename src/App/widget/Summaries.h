/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The summaries screen: the running or cycling totals per week, month or
 * year, as a bar chart with a table of the same under it, or the totals so
 * far through a year, against the two years before, as lines, or fitness,
 * fatigue and form through a year.
 */

#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <BarChart.h>
#include <LineChart.h>
#include <Table.h>
#include <Widget.h>

#include <storage/Fitness.h>
#include <storage/PaceCurve.h>
#include <storage/PowerCurve.h>
#include <storage/Summaries.h>
#include <storage/Thresholds.h>
#include <storage/Volume.h>

namespace ST {

enum class Graph {
    Bars,
    YearToDate, // The lines of the totals so far
    Fitness,
    Curve, // The best power or pace, by the sport
};

// What the screen shows, and the years it can go between.
struct SummaryView {
    VolumeSport             sport { VolumeSport::Running };
    Metric                  metric { Metric::Distance };
    Period                  period { Period::Week };
    int                     year { 0 };
    std::optional<unsigned> month { }; // A month's weeks, drilled down to
    Graph                   graph { Graph::Bars };
    int                     first_year { 0 };
    int                     last_year { 0 };
    // Of any sport, for the fitness.
    std::pair<int, int> fitness_years { };
    // The last day of the curve's window.
    std::chrono::year_month_day curve_end { };

    // The months a month's weeks go between, if they're set: else the
    // years' Januaries and Decembers.
    std::chrono::year_month first_month { };
    std::chrono::year_month last_month { };

    // The years there are totals of; `year` stays between them.
    void set_years(std::pair<int, int> years);
    // A year back or forward, or a month in a month's weeks, if there's
    // one: not by year, as all of them are shown then. Whether it changed.
    bool step_year(int step);
};

// "Cycling · distance by week · 2025", "… by week · March 2025", or, by
// year, without the year; the lines "Cycling · distance to date · 2025".
[[nodiscard]] std::string volume_heading(SummaryView const &view);

// A bar per period, in the unit the scale shows: km, hours, a count, m.
[[nodiscard]] std::vector<Bar> volume_bars(std::vector<PeriodTotal> const &totals, Period period, Metric metric);

// The table's columns and rows: a row per period, and their total.
[[nodiscard]] std::vector<TableColumn> volume_columns(Period period, VolumeSport sport);
[[nodiscard]] std::vector<TableRow>    volume_rows(std::vector<PeriodTotal> const &totals, Period period);

// "Fitness · CTL 72 · ATL 85 · TSB -13 · +4.2 a week · 2026 · 3 without
// TSS", on `day`, today or the year's last; or, without thresholds, what's
// wrong with the ones in the settings, or that there are none.
[[nodiscard]] std::string fitness_heading(std::vector<FitnessDay> const &days, std::chrono::year_month_day day, size_t without,
    bool has_thresholds, std::string_view thresholds_error = { });

// The fitness table's: a row per week, its TSS and CTL, ATL and TSB at
// its end; without thresholds, how to set them.
[[nodiscard]] std::vector<TableColumn> fitness_columns(bool has_thresholds);
[[nodiscard]] std::vector<TableRow>    fitness_rows(std::vector<FitnessWeek> const &weeks, bool has_thresholds);

// The year-to-date table's: a row per month, with the totals so far in
// the year and the year before, and the difference, signed.
[[nodiscard]] std::vector<TableColumn> year_to_date_columns(int year);
[[nodiscard]] std::vector<TableRow>    year_to_date_rows(std::vector<YearToDateRow> const &rows, Metric metric);

constexpr int CurveWindowDays = 90;
constexpr int CurveStepDays = 28;
// Along the curve's x axis, at evenly spaced logarithms of the time or
// the distance.
constexpr size_t CurvePoints = 200;
// The durations of the power table's rows, in seconds.
constexpr std::array<u32, 11> PowerTableDurations { 5, 15, 30, 60, 120, 300, 600, 1200, 1800, 3600, 7200 };

// "Power · CP 288 W · W' 18 kJ · FTP 275 W · 90 days to 12 Oct 2026",
// without CP if there's none, and without the FTP if there's no ride or
// no FTP.
[[nodiscard]] std::string power_heading(std::vector<PowerBest> const &window, std::optional<CriticalPower> const &cp,
    std::optional<float64> ftp, std::chrono::year_month_day end);

// The power table's rows: the best for each of PowerTableDurations a
// ride in the window lasted.
struct PowerRow {
    u32       seconds { 0 };
    PowerBest best { };
};

[[nodiscard]] std::vector<PowerRow> power_table(std::vector<PowerBest> const &bests);

// "5 s", "1 min", "2 h".
[[nodiscard]] std::string format_duration(u32 seconds);

[[nodiscard]] std::vector<TableColumn> power_columns();
[[nodiscard]] std::vector<TableRow>    power_rows(std::vector<PowerRow> const &rows, ActivitySummaries const &summaries,
    std::function<std::string(ActivitySummary const &)> const &name_of);

// The curve at CurvePoints from 1 s to `longest` seconds, evenly
// spaced in the logarithm of the time, interpolated between the
// durations: up to the last of `bests`, so a shorter curve stops partway.
[[nodiscard]] std::vector<double>    curve_line(std::vector<PowerBest> const &bests, u32 longest);
[[nodiscard]] std::vector<ChartTick> curve_ticks(u32 longest);

// The keys line under the heading: Left and Right move the power
// curve's window by weeks, the rest by year.
[[nodiscard]] std::string summaries_keys(Graph graph);

// The distances of the pace table's rows, in metres.
constexpr std::array<u32, 7> PaceTableDistances { 400, 1000, 1609, 5000, 10000, 21097, 42195 };
// The pace scale's steps, in seconds a km.
constexpr std::array<double, 7> PaceSteps { 5, 10, 15, 30, 60, 120, 300 };

// "Pace · CS 4:12 /km · D' 210 m · 90 days to 12 Oct 2026", without CS
// if there's none; "Pace · no runs · …" without any.
[[nodiscard]] std::string pace_heading(std::vector<PaceBest> const &window, std::optional<CriticalSpeed> const &cs,
    std::chrono::year_month_day end);

// The pace table's rows: the best for each of PaceTableDistances a run in
// the window covered.
struct PaceRow {
    u32      metres { 0 };
    PaceBest best { };
};

[[nodiscard]] std::vector<PaceRow> pace_table(std::vector<PaceBest> const &bests);

// "400 m", "5 km", "1 mile", "Half marathon".
[[nodiscard]] std::string format_race_distance(u32 metres);

[[nodiscard]] std::vector<TableColumn> pace_columns();
[[nodiscard]] std::vector<TableRow>    pace_rows(std::vector<PaceRow> const &rows, ActivitySummaries const &summaries,
    std::function<std::string(ActivitySummary const &)> const &name_of);

// The paces, s/km, from 400 m to `longest` metres, as curve_line() has
// the power.
[[nodiscard]] std::vector<double>    pace_line(std::vector<PaceBest> const &bests, u32 longest);
[[nodiscard]] std::vector<ChartTick> pace_ticks(u32 longest);

// A total's time in hours and minutes, "312:10": a year's seconds mean
// little.
[[nodiscard]] std::string format_total_time(u64 seconds);

struct SummariesScreen : public Layout {
    static constexpr int TableRows = 8;
    static constexpr int ComparedYears = 2; // The years before, on the lines

    SummaryView                 view { };
    std::vector<SummaryView>    trail { }; // The views drilled down from
    ActivitySummaries           summaries { };
    ThresholdList               thresholds { };
    std::string                 thresholds_error { }; // Why the settings' can't be read
    std::chrono::year_month_day today { };
    std::shared_ptr<Label>      heading;
    std::shared_ptr<Label>      keys;
    std::shared_ptr<BarChart>   chart;
    std::shared_ptr<LineChart>  lines;
    std::shared_ptr<Table>      table;
    std::vector<PeriodTotal>    totals { }; // The bars', none for the lines
    // Drilling into a week: its first activity, to list.
    std::function<void(ActivitySummary const &)> on_week { };
    // Enter on a row of the power table: its ride, to show.
    std::function<void(ActivitySummary const &)> on_ride { };
    // A ride's name, for the power table, kept by file while the screen
    // is open.
    std::function<std::string(ActivitySummary const &)> ride_name { };
    std::map<std::string, std::string>                  ride_names { };
    // The summary of each of the curve table's rows, for Enter.
    std::vector<size_t> curve_rides { };

    explicit SummariesScreen(pWidget const &parent);
    void initialize() override;
    void on_draw() override;

    // Opens on today's year, by week, on `sport`.
    void open(VolumeSport sport, std::chrono::year_month_day today_date);
    // New summaries, as the job reads them: the view stays as it is.
    void set_summaries(ActivitySummaries new_summaries);
    // W, M, Y: bars by that period, at the top.
    void set_period(Period period);
    void set_sport(VolumeSport sport);
    void set_metric(Metric metric);
    // G: the lines, or back to the bars, at the top.
    void toggle_lines();
    // F: the fitness, or back to the bars, at the top.
    void toggle_fitness();
    // P: the power or pace curve of the 90 days up to today, by the
    // sport, or back to the bars.
    void toggle_curve();
    // A year back or forward; in the curve, 4 weeks.
    void step_year(int step);
    // Up and Down through the table's rows.
    void step_row(int step);
    // Enter, or a click on a bar: into the selected period, or `bar`; in
    // the curve, the selected row's ride or run.
    void drill(std::optional<size_t> bar = { });
    // Backspace and Esc: back up to the view drilled down from. False at
    // the top.
    bool up();

private:
    void               refresh();
    void               refresh_in_place();
    void               refresh_lines();
    void               refresh_fitness();
    void               refresh_power();
    void               refresh_pace();
    bool               step_curve(int step);
    std::string const &name_of(ActivitySummary const &summary);
    void               show_graph(Graph graph);
    void               select_period(std::chrono::year_month_day first_day);
    // The years and months the view can go between, for its sport.
    void set_span();
};

}
