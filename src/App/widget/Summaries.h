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

#include <chrono>
#include <functional>
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
#include <storage/Summaries.h>
#include <storage/Thresholds.h>
#include <storage/Volume.h>

namespace ST {

enum class Graph {
    Bars,
    YearToDate, // The lines of the totals so far
    Fitness,
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
// TSS", on `day`, today or the year's last; or what to set without
// thresholds.
[[nodiscard]] std::string fitness_heading(std::vector<FitnessDay> const &days, std::chrono::year_month_day day, size_t without, bool has_thresholds);

// The fitness table's: a row per week, its TSS and CTL, ATL and TSB at
// its end; without thresholds, how to set them.
[[nodiscard]] std::vector<TableColumn> fitness_columns(bool has_thresholds);
[[nodiscard]] std::vector<TableRow>    fitness_rows(std::vector<FitnessWeek> const &weeks, bool has_thresholds);

// The year-to-date table's: a row per month, with the totals so far in
// the year and the year before, and the difference, signed.
[[nodiscard]] std::vector<TableColumn> year_to_date_columns(int year);
[[nodiscard]] std::vector<TableRow>    year_to_date_rows(std::vector<YearToDateRow> const &rows, Metric metric);

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
    std::chrono::year_month_day today { };
    std::shared_ptr<Label>      heading;
    std::shared_ptr<Label>      keys;
    std::shared_ptr<BarChart>   chart;
    std::shared_ptr<LineChart>  lines;
    std::shared_ptr<Table>      table;
    std::vector<PeriodTotal>    totals { }; // The bars', none for the lines
    // Drilling into a week: its first activity, to list.
    std::function<void(ActivitySummary const &)> on_week { };

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
    void step_year(int step);
    // Up and Down through the table's rows.
    void step_row(int step);
    // Enter, or a click on a bar: into the selected period, or `bar`.
    void drill(std::optional<size_t> bar = { });
    // Backspace and Esc: back up to the view drilled down from. False at
    // the top.
    bool up();

private:
    void refresh();
    void refresh_in_place();
    void refresh_lines();
    void refresh_fitness();
    void show_graph(Graph graph);
    void select_period(std::chrono::year_month_day first_day);
    // The years and months the view can go between, for its sport.
    void set_span();
};

}
