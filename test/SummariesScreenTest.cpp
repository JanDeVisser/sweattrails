/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the summaries screen without a window: the view's years, the
 * heading, the bars and the table's rows, and the keys' effects.
 */

#include <ctime>
#include <memory>

#include <widget/Summaries.h>

#include <TestApp.h>

using namespace ST;

namespace {

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
    screen->set_summaries({ run(2021, 3, 1), ride(2023, 3, 6), ride(2023, 3, 8), ride(2025, 1, 6), ride(2025, 3, 3) });
    screen->open(VolumeSport::Cycling, 2026);
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
    screen->open(VolumeSport::Running, 2026);
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
    screen->open(VolumeSport::Cycling, 2026);
    CHECK_EQ(screen->view.year, 2026);
    CHECK_EQ(screen->view.first_year, 2023);
    screen->set_summaries({ ride(2023, 3, 6), ride(2026, 3, 2) });
    CHECK_EQ(screen->view.year, 2026);
    CHECK_EQ(screen->table->rows.back()[3], "1");
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
    return Test::test_result();
}
