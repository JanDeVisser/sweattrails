/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The summaries screen: the running or cycling totals per week, month or
 * year, as a bar chart with a table of the same under it.
 */

#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <BarChart.h>
#include <Table.h>
#include <Widget.h>

#include <storage/Summaries.h>
#include <storage/Volume.h>

namespace ST {

// What the screen shows, and the years it can go between.
struct SummaryView {
    VolumeSport sport { VolumeSport::Running };
    Metric      metric { Metric::Distance };
    Period      period { Period::Week };
    int         year { 0 };
    int         first_year { 0 };
    int         last_year { 0 };

    // The years there are totals of; `year` stays between them.
    void set_years(std::pair<int, int> years);
    // A year back or forward, if there's one: not by year, as all of them
    // are shown then. Whether the year changed.
    bool step_year(int step);
};

// "Cycling · distance by week · 2025", or, by year, without the year.
[[nodiscard]] std::string volume_heading(SummaryView const &view);

// A bar per period, in the unit the scale shows: km, hours, a count, m.
[[nodiscard]] std::vector<Bar> volume_bars(std::vector<PeriodTotal> const &totals, Period period, Metric metric);

// The table's columns and rows: a row per period, and their total.
[[nodiscard]] std::vector<TableColumn> volume_columns(Period period, VolumeSport sport);
[[nodiscard]] std::vector<TableRow>    volume_rows(std::vector<PeriodTotal> const &totals, Period period);

// A total's time in hours and minutes, "312:10": a year's seconds mean
// little.
[[nodiscard]] std::string format_total_time(u64 seconds);

struct SummariesScreen : public Layout {
    static constexpr int TableRows = 8;

    SummaryView               view { };
    ActivitySummaries         summaries { };
    int                       this_year { 0 };
    std::shared_ptr<Label>    heading;
    std::shared_ptr<Label>    keys;
    std::shared_ptr<BarChart> chart;
    std::shared_ptr<Table>    table;
    std::vector<PeriodTotal>  totals { };

    explicit SummariesScreen(pWidget const &parent);
    void initialize() override;
    void on_draw() override;

    // Opens on `today`'s year, by week, on `sport`.
    void open(VolumeSport sport, int today_year);
    // New summaries, as the job reads them: the view stays as it is.
    void set_summaries(ActivitySummaries new_summaries);
    void set_period(Period period);
    void set_sport(VolumeSport sport);
    void set_metric(Metric metric);
    void step_year(int step);
    // Up and Down through the table's rows.
    void step_row(int step);

private:
    void refresh();
    void refresh_in_place();
};

}
