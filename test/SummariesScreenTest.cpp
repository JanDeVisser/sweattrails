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
    return Test::test_result();
}
