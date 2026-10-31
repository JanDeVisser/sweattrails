/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the summaries screen without a window: the view's years, the
 * heading, the bars and the table's rows, and the keys' effects.
 */

#include <chrono>
#include <cmath>
#include <ctime>
#include <format>
#include <memory>

#include <widget/Summaries.h>

#include <TestApp.h>

using namespace ST;

namespace {

// What the screen takes as today, in its tests.
constexpr auto Today = std::chrono::year { 2026 } / std::chrono::June / 15;

i64 local_noon(int year, unsigned month, unsigned day)
{
    std::tm tm {
        .tm_sec = 0,
        .tm_min = 0,
        .tm_hour = 12,
        .tm_mday = static_cast<int>(day),
        .tm_mon = static_cast<int>(month) - 1,
        .tm_year = year - 1900,
        .tm_isdst = -1,
    };
    return static_cast<i64>(std::mktime(&tm));
}

ActivitySummary ride(int year, unsigned month, unsigned day)
{
    return ActivitySummary {
        .start = local_noon(year, month, day),
        .sport = FIT::sport::Cycling,
        .distance = 42345,
        .moving = 5430,
        .ascent = 410,
    };
}

ActivitySummary run(int year, unsigned month, unsigned day)
{
    return ActivitySummary { .start = local_noon(year, month, day), .sport = FIT::sport::Running, .distance = 10000, .moving = 3000 };
}

std::shared_ptr<SummariesScreen> make_screen()
{
    auto screen = Widget::make<SummariesScreen>(nullptr);
    // A size to lay out the bars or the lines in, as the app's has.
    screen->viewport = Rect<float> { 0, 0, 1000, 800 };
    screen->set_summaries({ run(2021, 3, 1), ride(2023, 3, 6), ride(2023, 3, 8), ride(2025, 1, 6), ride(2025, 3, 3) });
    screen->open(VolumeSport::Cycling, Today);
    return screen;
}

void test_view_years()
{
    SummaryView view { .year = 2030 };
    view.set_years({ 2020, 2026 });
    CHECK_EQ(view.year, 2026);
    CHECK(view.step_year(-1));
    CHECK_EQ(view.year, 2025);
    view.year = 2020;
    CHECK(!view.step_year(-1));
    CHECK_EQ(view.year, 2020);
    view.period = Period::Year;
    CHECK(!view.step_year(1));
}

void test_heading()
{
    SummaryView view { .sport = VolumeSport::Cycling, .metric = Metric::Distance, .period = Period::Week, .year = 2025 };
    CHECK_EQ(volume_heading(view), "Cycling · distance by week · 2025");
    view.period = Period::Year;
    view.metric = Metric::Ascent;
    CHECK_EQ(volume_heading(view), "Cycling · ascent by year");
}

void test_format_total_time()
{
    CHECK_EQ(format_total_time(0), "0:00");
    CHECK_EQ(format_total_time(5430), "1:30");
    CHECK_EQ(format_total_time(312 * 3600 + 10 * 60 + 59), "312:10");
}

// The weeks' bars are labelled at the first week of each month only.
void test_bars()
{
    ActivitySummaries const summaries { ride(2025, 1, 6), ride(2025, 1, 7) };
    auto const              weeks = volume_totals(summaries, VolumeSport::Cycling, Period::Week, 2025);
    auto const              bars = volume_bars(weeks, Period::Week, Metric::Distance);
    CHECK_EQ(bars.size(), 52uz);
    CHECK_EQ(bars[0].label, "Jan"); // Monday 6 January
    CHECK_EQ(bars[1].label, "");
    CHECK_EQ(bars[4].label, "Feb"); // Monday 3 February
    CHECK_EQ(bars[0].value, 84.69);
    auto const hours = volume_bars(weeks, Period::Week, Metric::Time);
    CHECK_EQ(hours[0].value, 10860.0 / 3600);
    auto const count = volume_bars(weeks, Period::Week, Metric::Count);
    CHECK_EQ(count[0].value, 2.0);
    auto const ascent = volume_bars(weeks, Period::Week, Metric::Ascent);
    CHECK_EQ(ascent[0].value, 820.0);

    auto const months = volume_totals(summaries, VolumeSport::Cycling, Period::Month, 2025);
    auto const month_bars = volume_bars(months, Period::Month, Metric::Distance);
    CHECK_EQ(month_bars[11].label, "Dec");
}

void test_rows()
{
    ActivitySummaries const summaries { ride(2025, 1, 6), ride(2025, 3, 3) };
    auto const              months = volume_totals(summaries, VolumeSport::Cycling, Period::Month, 2025);
    auto const              rows = volume_rows(months, Period::Month);
    CHECK_EQ(rows.size(), 13uz);
    CHECK((rows[0] == TableRow { "January", "42.3 km", "1:30", "1", "410 m" }));
    CHECK((rows[1] == TableRow { "February", "0.0 km", "0:00", "0", "0 m" }));
    CHECK((rows[12] == TableRow { "Total", "84.7 km", "3:01", "2", "820 m" }));

    auto const weeks = volume_totals(summaries, VolumeSport::Cycling, Period::Week, 2025);
    auto const week_rows = volume_rows(weeks, Period::Week);
    CHECK_EQ(week_rows[0][0], "Mon 6 Jan");
    auto const years = volume_totals(summaries, VolumeSport::Cycling, Period::Year, 2026);
    auto const year_rows = volume_rows(years, Period::Year);
    CHECK_EQ(year_rows[0][0], "2025");
    auto const running = volume_columns(Period::Week, VolumeSport::Running);
    CHECK_EQ(running[3].header, "Runs");
    CHECK_EQ(running[0].header, "Week");
}

void test_open()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    CHECK_EQ(screen->view.year, 2026);
    CHECK(screen->view.period == Period::Week);
    CHECK_EQ(screen->view.first_year, 2023);
    CHECK_EQ(screen->heading->text, "Cycling · distance by week · 2026");
    CHECK_EQ(screen->chart->bars.size(), 52uz);
    CHECK_EQ(screen->table->rows.size(), 53uz);
}

// Left and Right stop at the sport's first year and this one.
void test_step_year()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->step_year(1);
    CHECK_EQ(screen->view.year, 2026);
    for (auto ix = 0; ix < 5; ++ix) {
        screen->step_year(-1);
    }
    CHECK_EQ(screen->view.year, 2023);
    screen->set_period(Period::Month);
    CHECK_EQ(screen->table->rows[2][3], "2");
    // Running goes back to 2021.
    screen->set_sport(VolumeSport::Running);
    CHECK_EQ(screen->view.first_year, 2021);
    CHECK_EQ(screen->table->rows[2][3], "0");
    CHECK_EQ(screen->heading->text, "Running · distance by month · 2023");
}

void test_years_view()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->set_period(Period::Year);
    CHECK_EQ(screen->chart->bars.size(), 4uz); // 2023 to 2026
    CHECK_EQ(screen->table->rows[0][0], "2023");
}

// The metric changes the bars, not the rows, and keeps the selection.
void test_metric_keeps_the_selection()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->set_period(Period::Month);
    screen->step_year(-1);
    screen->table->select(2);
    screen->set_metric(Metric::Count);
    CHECK(screen->table->selection == 2uz);
    CHECK_EQ(screen->chart->bars[2].value, 1.0);
    CHECK_EQ(screen->chart->min_step, 1.0);
}

void test_step_row()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->set_period(Period::Month);
    screen->step_row(1);
    CHECK(screen->table->selection == 0uz);
    screen->step_row(-1);
    CHECK(screen->table->selection == 0uz);
    for (auto ix = 0; ix < 20; ++ix) {
        screen->step_row(1);
    }
    CHECK(screen->table->selection == 12uz); // The total
    // The total has no bar to highlight.
    screen->on_draw();
    CHECK(!screen->chart->highlight);
    screen->step_row(-1);
    screen->on_draw();
    CHECK(screen->chart->highlight == 11uz);
}

void test_hover_selects_the_row()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->chart->on_hover(5);
    CHECK(screen->table->selection == 5uz);
    // Leaving the bars keeps it.
    screen->chart->on_hover({ });
    CHECK(screen->table->selection == 5uz);
}

// The job reads more summaries: the view and the selection stay.
void test_new_summaries()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->set_period(Period::Month);
    screen->step_year(-1);
    screen->table->select(0);
    auto more = screen->summaries;
    more.push_back(ride(2025, 1, 20));
    screen->set_summaries(more);
    CHECK_EQ(screen->view.year, 2025);
    CHECK(screen->table->selection == 0uz);
    CHECK_EQ(screen->table->rows[0][3], "2");
}

// The job sends new summaries after each chunk of the first build. A table
// the user scrolled through, with nothing selected, stays where it is.
void test_new_summaries_keep_the_scroll()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->step_year(-1); // 2025: 52 weeks
    screen->table->scroll.top_line = 20;
    screen->set_summaries(screen->summaries);
    CHECK_EQ(screen->table->scroll.top_line, 20);
}

// The years view gains a year before the first one shown: the selection
// stays on the year it was on, not on the row it was in.
void test_new_summaries_keep_the_selected_year()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->set_period(Period::Year);
    screen->table->select(2); // 2025
    auto earlier = screen->summaries;
    earlier.insert(earlier.begin(), ride(2020, 5, 1));
    screen->set_summaries(earlier);
    CHECK_EQ(screen->table->rows[0][0], "2020");
    CHECK(screen->table->selection == 5uz);
    // And the total stays the total.
    screen->step_row(10);
    screen->set_summaries(screen->summaries);
    CHECK(screen->table->selection == screen->table->rows.size() - 1);
}

// The mouse rests on a bar, which selects its row; Down then selects the
// next one. Neither new summaries nor another metric should put the
// selection back on the bar under the mouse, which didn't move.
void test_hover_doesnt_undo_the_keys()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    auto const         &chart = screen->chart;
    // Without a window the mouse is at (0, 0): put bar 5 of 52 under it.
    chart->scale_width = 100;
    chart->label_height = 20;
    chart->viewport = Rect<float> { -(100 + 5.5f * 10), -100, 100 + 52 * 10 + 10, 300 };
    chart->process_input();
    CHECK(screen->table->selection == 5uz);
    screen->step_row(1);
    chart->process_input();
    CHECK(screen->table->selection == 6uz);

    screen->set_summaries(screen->summaries);
    chart->process_input();
    CHECK(screen->table->selection == 6uz);

    screen->set_metric(Metric::Time);
    chart->process_input();
    CHECK(screen->table->selection == 6uz);
}

// An empty store, as on the first start before the job read anything:
// every view has its periods, with nothing in them, and nothing crashes.
void test_empty_store()
{
    Test::ScopedTestApp app;
    auto                screen = Widget::make<SummariesScreen>(nullptr);
    screen->set_summaries({ });
    screen->open(VolumeSport::Running, Today);
    CHECK_EQ(screen->view.first_year, 2026);
    CHECK_EQ(screen->chart->bars.size(), 52uz);
    CHECK_EQ(screen->table->rows.size(), 53uz);
    CHECK((screen->table->rows.back() == TableRow { "Total", "0.0 km", "0:00", "0", "0 m" }));
    screen->step_year(-1);
    CHECK_EQ(screen->view.year, 2026);
    screen->set_period(Period::Year);
    CHECK_EQ(screen->chart->bars.size(), 1uz);
    CHECK_EQ(screen->table->rows.size(), 2uz);
    screen->step_row(-1);
    CHECK(screen->table->selection == 1uz);
    screen->on_draw();
    CHECK(!screen->chart->highlight);
    CHECK((screen->chart->scale() == ChartScale { .top = 1, .step = 1 }));
    screen->set_metric(Metric::Count);
    screen->set_period(Period::Month);
    CHECK_EQ(screen->chart->bars.size(), 12uz);
}

// The summaries come in before the screen is ever opened: opening it
// then still starts on this year, by week.
void test_summaries_before_open()
{
    Test::ScopedTestApp app;
    auto                screen = Widget::make<SummariesScreen>(nullptr);
    screen->set_summaries({ ride(2023, 3, 6) });
    screen->open(VolumeSport::Cycling, Today);
    CHECK_EQ(screen->view.year, 2026);
    CHECK_EQ(screen->view.first_year, 2023);
    screen->set_summaries({ ride(2023, 3, 6), ride(2026, 3, 2) });
    CHECK_EQ(screen->view.year, 2026);
    CHECK_EQ(screen->table->rows.back()[3], "1");
}

std::chrono::year_month_day date(int year, unsigned month, unsigned day)
{
    return std::chrono::year { year } / std::chrono::month { month } / std::chrono::day { day };
}

void test_year_to_date_heading()
{
    SummaryView view { .sport = VolumeSport::Cycling, .metric = Metric::Time, .year = 2025, .graph = Graph::YearToDate };
    CHECK_EQ(volume_heading(view), "Cycling · time to date · 2025");
    view.graph = Graph::Bars;
    view.month = 3;
    CHECK_EQ(volume_heading(view), "Cycling · time by week · March 2025");
}

// The difference is signed, and there's no total yet after today.
void test_year_to_date_rows()
{
    std::vector<YearToDateRow> const rows {
        { .month = std::chrono::January, .year = 30000.0, .before = 40000.0 },
        { .month = std::chrono::February, .year = 150000.0, .before = 100000.0 },
        { .month = std::chrono::March, .before = 100000.0 },
    };
    auto const table = year_to_date_rows(rows, Metric::Distance);
    CHECK((table[0] == TableRow { "January", "30.0 km", "40.0 km", "-10.0 km" }));
    CHECK((table[1] == TableRow { "February", "150.0 km", "100.0 km", "+50.0 km" }));
    CHECK((table[2] == TableRow { "March", "", "100.0 km", "" }));
    auto const time = year_to_date_rows({ { .month = std::chrono::January, .year = 3600.0, .before = 6300.0 } }, Metric::Time);
    CHECK((time[0] == TableRow { "January", "1:00", "1:45", "-0:45" }));
    auto const columns = year_to_date_columns(2026);
    CHECK_EQ(columns[1].header, "2026");
    CHECK_EQ(columns[2].header, "2025");
}

// G shows the lines, with the table of months, and back.
void test_lines()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->step_year(-1); // 2025
    screen->toggle_lines();
    CHECK(screen->view.graph == Graph::YearToDate);
    CHECK(screen->chart->policy == SizePolicy::Hide);
    CHECK(screen->lines->policy == SizePolicy::Stretch);
    CHECK_EQ(screen->heading->text, "Cycling · distance to date · 2025");
    CHECK_EQ(screen->table->rows.size(), 12uz);
    CHECK_EQ(screen->table->rows[2][1], "84.7 km");
    CHECK_EQ(screen->table->rows[2][2], "0.0 km");
    // 2025, 2024 and 2023, the year of the first rides.
    CHECK_EQ(screen->lines->series.size(), 3uz);
    CHECK(screen->lines->series[0].bold);
    CHECK_EQ(screen->lines->series[0].values.size(), 365uz);
    CHECK_EQ(screen->lines->series[0].values.back(), 84.69);
    CHECK_EQ(screen->lines->ticks.size(), 12uz);
    CHECK_EQ(screen->lines->ticks[2].x, 59.0); // 1 March
    screen->step_year(-1);
    CHECK_EQ(screen->lines->series.size(), 2uz); // 2024, 2023
    screen->step_year(-1);
    CHECK_EQ(screen->lines->series.size(), 1uz); // 2023 alone
    screen->toggle_lines();
    CHECK(screen->view.graph == Graph::Bars);
    CHECK(screen->chart->policy == SizePolicy::Stretch);
    CHECK_EQ(screen->chart->bars.size(), 52uz);
}

// This year's line stops at today.
void test_line_stops_at_today()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->toggle_lines();
    CHECK_EQ(screen->lines->series[0].values.size(), 166uz); // To 15 June
    // As wide as 2024's line, two years back, a leap year.
    CHECK_EQ(screen->lines->x_count, 366uz);
    CHECK_EQ(screen->table->rows[5][0], "June to the 15th");
    CHECK_EQ(screen->table->rows[6][1], ""); // July is to come
    CHECK_EQ(screen->table->rows[4][1], "0.0 km");
}

// W, M or Y in the lines go back to the bars by that period.
void test_period_leaves_the_lines()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->toggle_lines();
    screen->set_period(Period::Month);
    CHECK(screen->view.graph == Graph::Bars);
    CHECK_EQ(screen->chart->bars.size(), 12uz);
}

// From the years to 2023's months, to March's weeks, to a week's first
// ride; and back up, with what was drilled into selected.
void test_drill_down_and_up()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    std::optional<i64>  listed { };
    screen->on_week = [&listed](ActivitySummary const &summary) { listed = summary.start; };
    screen->set_period(Period::Year);
    screen->table->select(0); // 2023
    screen->drill();
    CHECK(screen->view.period == Period::Month);
    CHECK_EQ(screen->view.year, 2023);
    CHECK_EQ(screen->trail.size(), 1uz);
    screen->drill(2); // March, by its bar
    CHECK(screen->view.period == Period::Week);
    CHECK(screen->view.month == 3u);
    CHECK_EQ(screen->chart->bars.size(), 4uz); // Mondays 6, 13, 20, 27 March
    CHECK_EQ(screen->heading->text, "Cycling · distance by week · March 2023");
    screen->drill(1); // No rides
    CHECK(!listed);
    screen->drill(0); // Two rides: the first
    CHECK(listed == local_noon(2023, 3, 6));

    CHECK(screen->up());
    CHECK(screen->view.period == Period::Month);
    CHECK(!screen->view.month);
    CHECK(screen->table->selection == 2uz); // March
    CHECK(screen->up());
    CHECK(screen->view.period == Period::Year);
    CHECK(screen->table->selection == 0uz); // 2023
    CHECK(!screen->up());
}

// Starting from the weeks: a week drills to its ride; a month from the
// months view to its weeks, and back up to the months.
void test_drill_from_weeks_and_months()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    std::optional<i64>  listed { };
    screen->on_week = [&listed](ActivitySummary const &summary) { listed = summary.start; };
    screen->step_year(-1); // 2025
    screen->drill(0);      // Monday 6 January
    CHECK(listed == local_noon(2025, 1, 6));
    CHECK(screen->trail.empty());
    screen->set_period(Period::Month);
    screen->drill(0);
    CHECK(screen->view.month == 1u);
    CHECK(screen->up());
    CHECK(screen->view.period == Period::Month);
    CHECK(!screen->up());
}

// The total row, the lines and no selection have nothing to drill into.
void test_nothing_to_drill()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->set_period(Period::Year);
    screen->drill();
    CHECK(screen->trail.empty());
    screen->table->select(screen->table->rows.size() - 1);
    screen->drill();
    CHECK(screen->trail.empty());
    screen->toggle_lines();
    screen->table->select(0);
    screen->drill();
    CHECK(screen->trail.empty());
}

// W, M, Y and G start afresh: nothing to go back up to.
void test_period_clears_the_trail()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->set_period(Period::Year);
    screen->drill(0);
    screen->drill(2);
    CHECK_EQ(screen->trail.size(), 2uz);
    screen->set_period(Period::Week);
    CHECK(screen->trail.empty());
    CHECK(!screen->view.month);
    CHECK_EQ(screen->chart->bars.size(), 52uz); // 2023's weeks
    screen->set_period(Period::Year);
    screen->drill(0);
    screen->toggle_lines();
    CHECK(screen->trail.empty());
}

// In a month's weeks, Left and Right go a month, across New Year, and
// stop at the years there are.
void test_step_by_month()
{
    SummaryView view { .period = Period::Week, .year = 2025, .month = 1u, .first_year = 2025, .last_year = 2026 };
    CHECK(view.step_year(-1) == false);
    CHECK(view.month == 1u);
    view.year = 2025;
    view.month = 12u;
    CHECK(view.step_year(1));
    CHECK_EQ(view.year, 2026);
    CHECK(view.month == 1u);
    CHECK(view.step_year(-1));
    CHECK_EQ(view.year, 2025);
    CHECK(view.month == 12u);
    view.year = 2026;
    CHECK(!view.step_year(12));
}

// The lines go by year also from the years view.
void test_lines_step_by_year()
{
    SummaryView view { .period = Period::Year, .year = 2026, .graph = Graph::YearToDate, .first_year = 2020, .last_year = 2026 };
    CHECK(view.step_year(-1));
    CHECK_EQ(view.year, 2025);
}

// Added in review.

// The month today is in has its total so far: only the months after it
// are empty, as the architecture says. On 15 January the whole year would
// otherwise be blank.
void test_year_to_date_this_month()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->set_summaries({ ride(2025, 6, 2), ride(2026, 6, 10) });
    screen->toggle_lines();
    CHECK_EQ(screen->view.year, 2026);
    CHECK_EQ(screen->table->rows[5][1], "42.3 km"); // June, so far
    CHECK_EQ(screen->table->rows[5][3], "+0.0 km");
    CHECK_EQ(screen->table->rows[6][1], ""); // July
}

// R or C deep down, then back up: the year stays in the sport's years,
// and what was drilled into is selected.
void test_up_after_changing_sport()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->set_sport(VolumeSport::Running); // 2021 to 2026
    screen->set_period(Period::Year);
    screen->drill(0);                        // 2021's months
    screen->drill(2);                        // March 2021's weeks
    screen->set_sport(VolumeSport::Cycling); // 2023 to 2026: March 2023
    CHECK_EQ(screen->view.year, 2023);
    CHECK(screen->up());
    CHECK(screen->view.period == Period::Month);
    CHECK(screen->view.year >= screen->view.first_year);
    CHECK_EQ(screen->view.year, 2023);
    CHECK(screen->table->selection == 2uz); // March
}

// Left in a month's weeks, across New Year, then back up: the months of
// the year of the weeks shown, with that month selected.
void test_up_after_stepping_months()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->set_period(Period::Month);
    screen->step_year(-1); // 2025
    screen->drill(2);      // March 2025's weeks
    screen->step_year(-1);
    screen->step_year(-1);
    screen->step_year(-1); // December 2024
    CHECK_EQ(screen->view.year, 2024);
    CHECK(screen->view.month == 12u);
    CHECK(screen->up());
    CHECK_EQ(screen->view.year, 2024);
    CHECK(screen->table->selection == 11uz); // December
}

// The job's chunks while the lines show: still the lines, the same row
// selected, the new numbers in.
void test_new_summaries_in_the_lines()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->step_year(-1); // 2025
    screen->toggle_lines();
    screen->table->select(3);
    screen->set_summaries({ run(2021, 3, 1), ride(2023, 3, 6), ride(2023, 3, 8), ride(2025, 1, 6), ride(2025, 3, 3), ride(2025, 2, 3) });
    CHECK(screen->view.graph == Graph::YearToDate);
    CHECK(screen->chart->policy == SizePolicy::Hide);
    CHECK(screen->table->selection == 3uz);
    CHECK_EQ(screen->table->rows.size(), 12uz);
    CHECK_EQ(screen->table->rows[1][1], "84.7 km"); // February, with the new ride
    CHECK_EQ(screen->lines->series[0].values.back(), 127.035);
}

// The job's chunks in a month's weeks: still that month, the same week
// selected, and the trail kept.
void test_new_summaries_in_a_month()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->set_period(Period::Year);
    screen->drill(0); // 2023's months
    screen->drill(2); // March 2023's weeks
    screen->table->select(1);
    screen->set_summaries({ run(2021, 3, 1), ride(2023, 3, 6), ride(2023, 3, 8), ride(2023, 3, 14), ride(2025, 1, 6), ride(2025, 3, 3) });
    CHECK(screen->view.month == 3u);
    CHECK_EQ(screen->view.year, 2023);
    CHECK_EQ(screen->trail.size(), 2uz);
    CHECK(screen->table->selection == 1uz);
    CHECK_EQ(screen->table->rows[1][3], "1");
}

// A leap year's lines: 366 days, and March starts a day later.
void test_lines_in_a_leap_year()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->step_year(-2); // 2024
    screen->toggle_lines();
    CHECK_EQ(screen->lines->x_count, 366uz);
    CHECK_EQ(screen->lines->series[0].values.size(), 366uz);
    CHECK_EQ(screen->lines->series[1].values.size(), 365uz); // 2023
    CHECK_EQ(screen->lines->ticks[2].x, 60.0);
    CHECK_EQ(screen->lines->ticks[11].x, 335.0); // 1 December
}

// The week of Monday 31 March lists its first ride, on 2 April.
void test_drill_into_a_week_across_months()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->set_summaries({ ride(2025, 3, 3), ride(2025, 4, 2), ride(2025, 4, 3) });
    std::optional<i64> listed { };
    screen->on_week = [&listed](ActivitySummary const &summary) { listed = summary.start; };
    screen->set_period(Period::Month);
    screen->step_year(-1); // 2025
    screen->drill(2);      // March's weeks
    CHECK_EQ(screen->chart->bars.size(), 5uz);
    screen->drill(4); // Monday 31 March
    CHECK(listed == local_noon(2025, 4, 2));
}

// Nothing in the store: the lines, drilling and going up don't fall over.
void test_empty_store_lines_and_drilling()
{
    Test::ScopedTestApp app;
    auto                screen = Widget::make<SummariesScreen>(nullptr);
    screen->viewport = Rect<float> { 0, 0, 1000, 800 };
    screen->set_summaries({ });
    screen->open(VolumeSport::Running, Today);
    bool listed = false;
    screen->on_week = [&listed](ActivitySummary const &) { listed = true; };
    screen->drill(0);
    CHECK(!listed);
    screen->toggle_lines();
    CHECK_EQ(screen->lines->series.size(), 1uz);
    CHECK_EQ(screen->lines->series[0].values.size(), 166uz);
    CHECK_EQ(screen->table->rows.size(), 12uz);
    CHECK(!screen->up());
    screen->set_period(Period::Year);
    screen->drill(0);
    CHECK(screen->view.period == Period::Month);
    screen->drill(0);
    CHECK(screen->view.month == 1u);
    CHECK(screen->up());
    CHECK(screen->up());
    CHECK(!screen->up());
}

// D, T, N, A in the lines: the lines and the table in that number.
void test_metric_in_the_lines()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->step_year(-1); // 2025
    screen->toggle_lines();
    screen->table->select(2);
    screen->set_metric(Metric::Count);
    CHECK(screen->view.graph == Graph::YearToDate);
    CHECK_EQ(screen->heading->text, "Cycling · count to date · 2025");
    CHECK_EQ(screen->table->rows[2][1], "2");
    CHECK_EQ(screen->table->rows[2][3], "+2");
    CHECK_EQ(screen->lines->min_step, 1.0);
    CHECK(screen->table->selection == 2uz);
}

// In a month's weeks, Right stops at this month, and Left at the sport's
// first activity's: the rides start in March 2023.
void test_step_by_month_stops_at_the_activities()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->set_period(Period::Month);
    screen->drill(5); // June 2026, this month
    CHECK(screen->view.month == 6u);
    screen->step_year(1);
    CHECK(screen->view.month == 6u);
    CHECK_EQ(screen->view.year, 2026);
    for (auto ix = 0; ix < 60; ++ix) {
        screen->step_year(-1);
    }
    CHECK_EQ(screen->view.year, 2023);
    CHECK(screen->view.month == 3u);
}

// 2023, the rides' first year: no 2022 to compare with, so its column and
// the difference are empty.
void test_no_year_before_the_first()
{
    Test::ScopedTestApp app;
    auto                screen = make_screen();
    screen->toggle_lines();
    for (auto ix = 0; ix < 3; ++ix) {
        screen->step_year(-1);
    }
    CHECK_EQ(screen->view.year, 2023);
    CHECK_EQ(screen->table->rows[2][1], "84.7 km");
    CHECK_EQ(screen->table->rows[2][2], "");
    CHECK_EQ(screen->table->rows[2][3], "");
}

// An hour at an FTP of 250 W, or at running's LTHR of 170 bpm: TSS 100.
ThresholdList fitness_thresholds()
{
    return { Thresholds { .from = std::chrono::year { 2020 } / 1 / 1, .ftp = 250, .lthr_running = 170, .lthr_cycling = 160 } };
}

ActivitySummary hard_ride(unsigned month, unsigned day)
{
    auto ret = ride(2026, month, day);
    ret.moving = 3600;
    ret.np = 250;
    return ret;
}

// A run of TSS 100 in 2021, rides of 100 every day from the 1st to the
// 14th of June 2026, and a ride without power or heart rate.
std::shared_ptr<SummariesScreen> make_fitness_screen(ThresholdList thresholds = fitness_thresholds())
{
    auto screen = Widget::make<SummariesScreen>(nullptr);
    screen->viewport = Rect<float> { 0, 0, 1000, 800 };
    auto hard_run = run(2021, 3, 1);
    hard_run.moving = 3600;
    hard_run.hr_load = 170.0 * 170 * 3600;
    ActivitySummaries summaries { hard_run, ride(2026, 5, 1) };
    for (auto day = 1u; day <= 14; ++day) {
        summaries.push_back(hard_ride(6, day));
    }
    screen->thresholds = std::move(thresholds);
    screen->set_summaries(summaries);
    screen->open(VolumeSport::Cycling, Today);
    return screen;
}

void test_fitness_heading()
{
    namespace chrono = std::chrono;
    std::vector<FitnessDay> days;
    for (auto day = 8u; day <= 15; ++day) {
        days.push_back(FitnessDay { .day = chrono::year { 2026 } / chrono::June / chrono::day { day } });
    }
    days.front().ctl = 68.2;
    days.back().ctl = 72.4;
    days.back().atl = 85.2;
    days.back().tsb = -12.6;
    CHECK_EQ(fitness_heading(days, Today, 3, true), "Fitness · CTL 72 · ATL 85 · TSB -13 · +4.2 a week · 2026 · 3 without TSS");
    CHECK_EQ(fitness_heading(days, Today, 0, true), "Fitness · CTL 72 · ATL 85 · TSB -13 · +4.2 a week · 2026");
    // A day without values, before the first activity.
    CHECK_EQ(fitness_heading(days, chrono::year { 2025 } / chrono::December / 31, 0, true), "Fitness · 2025");
    CHECK_EQ(fitness_heading(days, Today, 0, false), "Set your FTP and LTHR in settings.json to see your fitness");
}

// Long after the last activity, CTL still decays, by less than 0.05 a
// week: the ramp rate is 0.0, not "-0.0".
void test_fitness_heading_after_a_long_rest()
{
    namespace chrono = std::chrono;
    std::vector<FitnessDay> days;
    for (auto day = 8u; day <= 15; ++day) {
        days.push_back(FitnessDay { .day = chrono::year { 2026 } / chrono::June / chrono::day { day } });
    }
    days.front().ctl = 0.02;
    days.back().ctl = 0.01;
    CHECK_EQ(fitness_heading(days, Today, 0, true), "Fitness · CTL 0 · ATL 0 · TSB 0 · +0.0 a week · 2026");
}

void test_fitness_view()
{
    Test::ScopedTestApp app;
    auto                screen = make_fitness_screen();
    screen->toggle_fitness();
    CHECK(screen->view.graph == Graph::Fitness);
    CHECK(screen->heading->text.starts_with("Fitness · CTL "));
    CHECK(screen->heading->text.ends_with(" a week · 2026 · 1 without TSS"));
    CHECK(screen->chart->policy == SizePolicy::Hide);
    CHECK(screen->lines->policy == SizePolicy::Stretch);
    // Up to today, the 15th of June: 166 days.
    CHECK_EQ(screen->lines->series.size(), 3uz);
    if (screen->lines->series.size() == 3) {
        CHECK_EQ(screen->lines->series[0].label, "CTL");
        CHECK(screen->lines->series[0].bold);
        CHECK_EQ(screen->lines->series[2].label, "TSB");
        CHECK_EQ(screen->lines->series[0].values.size(), 166uz);
    }
    CHECK_EQ(screen->lines->x_count, 365uz);
    // Two weeks of hard rides: tired, and the scale goes below 0.
    CHECK(screen->lines->scale().bottom < 0);
    // The weeks from Monday the 5th of January to this one.
    auto const &rows = screen->table->rows;
    CHECK_EQ(rows.size(), 24uz);
    if (rows.size() == 24) {
        CHECK_EQ(rows[21][0], "Mon 1 Jun");
        CHECK_EQ(rows[21][1], "700");
        CHECK_EQ(rows[23][0], "Mon 15 Jun");
        CHECK_EQ(rows[23][1], "0");
    }
    // Nothing to drill into.
    screen->step_row(1);
    screen->drill();
    CHECK(screen->view.graph == Graph::Fitness);
    CHECK(!screen->up());
}

void test_fitness_by_year()
{
    Test::ScopedTestApp app;
    auto                screen = make_fitness_screen();
    screen->toggle_fitness();
    screen->step_year(-1);
    CHECK_EQ(screen->view.year, 2025);
    CHECK(screen->heading->text.ends_with(" a week · 2025"));
    // A past year is all of it, and all its weeks.
    if (!screen->lines->series.empty()) {
        CHECK_EQ(screen->lines->series[0].values.size(), 365uz);
    }
    CHECK_EQ(screen->table->rows.size(), 52uz);
    // Back to the first activity of any sport, the run of 2021.
    for (auto step = 0; step < 10; ++step) {
        screen->step_year(-1);
    }
    CHECK_EQ(screen->view.year, 2021);
    screen->step_year(1);
    CHECK_EQ(screen->view.year, 2022);
    // Leaving it, the year is among cycling's again.
    screen->set_period(Period::Month);
    CHECK(screen->view.graph == Graph::Bars);
    CHECK_EQ(screen->view.year, 2026);
}

void test_leaving_the_fitness()
{
    Test::ScopedTestApp app;
    auto                screen = make_fitness_screen();
    screen->toggle_fitness();
    auto const heading = screen->heading->text;
    // The fitness is of all sports, in TSS.
    screen->set_sport(VolumeSport::Running);
    screen->set_metric(Metric::Ascent);
    CHECK_EQ(screen->heading->text, heading);
    screen->toggle_lines();
    CHECK(screen->view.graph == Graph::YearToDate);
    CHECK_EQ(screen->heading->text, "Running · ascent to date · 2026");
    screen->toggle_fitness();
    screen->toggle_fitness();
    CHECK(screen->view.graph == Graph::Bars);
    CHECK_EQ(screen->heading->text, "Running · ascent by week · 2026");
}

void test_fitness_without_thresholds()
{
    Test::ScopedTestApp app;
    auto                screen = make_fitness_screen({ });
    screen->toggle_fitness();
    CHECK_EQ(screen->heading->text, "Set your FTP and LTHR in settings.json to see your fitness");
    CHECK(screen->lines->series.empty());
    CHECK_EQ(screen->table->rows.size(), 4uz);
    if (!screen->table->rows.empty()) {
        CHECK_EQ(screen->table->rows[0][0], R"("thresholds": [)");
    }
}

// A past year's last week runs into the next year, as on the bars: 2025's
// of Monday the 29th of December has the ride of Friday the 2nd of
// January 2026, and its CTL, ATL and TSB are Sunday the 4th's.
void test_fitness_last_week_into_the_new_year()
{
    Test::ScopedTestApp app;
    auto                screen = Widget::make<SummariesScreen>(nullptr);
    screen->viewport = Rect<float> { 0, 0, 1000, 800 };
    auto december = ride(2025, 12, 29);
    december.moving = 3600;
    december.np = 250;
    screen->thresholds = fitness_thresholds();
    screen->set_summaries({ december, hard_ride(1, 2) });
    screen->open(VolumeSport::Cycling, Today);
    screen->toggle_fitness();
    screen->step_year(-1);
    CHECK_EQ(screen->view.year, 2025);
    auto const &rows = screen->table->rows;
    CHECK(!rows.empty());
    if (rows.empty()) {
        return;
    }
    CHECK_EQ(rows.back()[0], "Mon 29 Dec");
    CHECK_EQ(rows.back()[1], "200");
    auto const days = fitness_days(screen->summaries, screen->thresholds, std::chrono::year { 2026 } / 1 / 4);
    CHECK_EQ(rows.back()[2], std::format("{}", std::llround(days.back().ctl)));
    CHECK_EQ(rows.back()[4], std::format("{}", std::llround(days.back().tsb)));
}

// The heading of a past year has its last day's values.
void test_fitness_heading_of_a_past_year()
{
    Test::ScopedTestApp app;
    auto                screen = make_fitness_screen();
    screen->toggle_fitness();
    screen->step_year(-5); // 2021, the run's
    CHECK_EQ(screen->view.year, 2021);
    auto const days = fitness_days(screen->summaries, screen->thresholds, std::chrono::year { 2021 } / 12 / 31);
    CHECK_EQ(screen->heading->text, fitness_heading(days, std::chrono::year { 2021 } / 12 / 31, 0, true));
    CHECK(screen->heading->text.ends_with(" a week · 2021"));
    // From New Year, at 0 before the run of the 1st of March.
    if (!screen->lines->series.empty()) {
        CHECK_EQ(screen->lines->series[0].values.size(), 365uz);
        CHECK_EQ(screen->lines->series[0].values[0], 0.0);
        CHECK(screen->lines->series[0].values[59] > 0);
    }
}

// Each key from the fitness view, and the year it leaves on.
void test_fitness_keys()
{
    Test::ScopedTestApp app;
    auto                screen = make_fitness_screen();
    // From the years view: Left and Right still go by year.
    screen->set_period(Period::Year);
    screen->toggle_fitness();
    screen->step_year(-1);
    CHECK_EQ(screen->view.year, 2025);
    CHECK(screen->heading->text.ends_with(" a week · 2025"));
    // Y back to the bars, by year.
    screen->set_period(Period::Year);
    CHECK(screen->view.graph == Graph::Bars);
    CHECK(screen->view.period == Period::Year);
    // W too, on cycling's only year.
    screen->toggle_fitness();
    screen->step_year(-5); // 2021
    CHECK_EQ(screen->view.year, 2021);
    screen->set_period(Period::Week);
    CHECK(screen->view.graph == Graph::Bars);
    CHECK_EQ(screen->view.year, 2026);
    // G to the lines, among cycling's years.
    screen->toggle_fitness();
    screen->step_year(-5);
    screen->toggle_lines();
    CHECK(screen->view.graph == Graph::YearToDate);
    CHECK_EQ(screen->view.year, 2026);
    CHECK_EQ(screen->heading->text, "Cycling · distance to date · 2026");
    // R in 2021 keeps the year, and F leaves on running's 2021.
    screen->toggle_fitness();
    screen->step_year(-5);
    screen->set_sport(VolumeSport::Running);
    CHECK_EQ(screen->view.year, 2021);
    CHECK(screen->view.graph == Graph::Fitness);
    screen->toggle_fitness();
    CHECK(screen->view.graph == Graph::Bars);
    CHECK_EQ(screen->view.year, 2021);
    CHECK_EQ(screen->heading->text, "Running · distance by week · 2021");
    // Right stops at this year.
    screen->toggle_fitness();
    for (auto step = 0; step < 10; ++step) {
        screen->step_year(1);
    }
    CHECK_EQ(screen->view.year, 2026);
}

// New summaries while the fitness is on show: the same week selected.
void test_new_summaries_in_the_fitness()
{
    Test::ScopedTestApp app;
    auto                screen = make_fitness_screen();
    screen->toggle_fitness();
    screen->table->select(21);
    auto summaries = screen->summaries;
    summaries.push_back(hard_ride(6, 15));
    screen->set_summaries(summaries);
    CHECK(screen->view.graph == Graph::Fitness);
    CHECK(screen->table->selection == 21uz);
    if (screen->table->rows.size() == 24) {
        CHECK_EQ(screen->table->rows[23][1], "100");
    }
}

}

int main()
{
    test_view_years();
    test_heading();
    test_format_total_time();
    test_bars();
    test_rows();
    test_open();
    test_step_year();
    test_years_view();
    test_metric_keeps_the_selection();
    test_step_row();
    test_hover_selects_the_row();
    test_new_summaries();
    test_new_summaries_keep_the_scroll();
    test_new_summaries_keep_the_selected_year();
    test_hover_doesnt_undo_the_keys();
    test_empty_store();
    test_summaries_before_open();
    test_year_to_date_heading();
    test_year_to_date_rows();
    test_lines();
    test_line_stops_at_today();
    test_period_leaves_the_lines();
    test_drill_down_and_up();
    test_drill_from_weeks_and_months();
    test_nothing_to_drill();
    test_period_clears_the_trail();
    test_step_by_month();
    test_lines_step_by_year();
    test_new_summaries_in_the_lines();
    test_new_summaries_in_a_month();
    test_lines_in_a_leap_year();
    test_drill_into_a_week_across_months();
    test_empty_store_lines_and_drilling();
    test_metric_in_the_lines();
    test_year_to_date_this_month();
    test_up_after_changing_sport();
    test_up_after_stepping_months();
    test_step_by_month_stops_at_the_activities();
    test_no_year_before_the_first();
    test_fitness_last_week_into_the_new_year();
    test_fitness_heading();
    test_fitness_view();
    test_fitness_by_year();
    test_leaving_the_fitness();
    test_fitness_without_thresholds();
    test_fitness_heading_of_a_past_year();
    test_fitness_keys();
    test_new_summaries_in_the_fitness();
    return Test::test_result();
}
