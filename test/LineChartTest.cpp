/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests LineChart's scale and where its values go. draw() needs fonts,
 * so the tests set the room for the scale and the labels themselves.
 */

#include <cmath>
#include <memory>

#include <LineChart.h>

#include <TestApp.h>

using namespace ST;

namespace {

// The plot from x 100 to 465 and y 10 to 270: 365 wide, 260 high.
std::shared_ptr<LineChart> make_chart()
{
    auto chart = std::make_shared<LineChart>(nullptr);
    chart->viewport = Rect<float> { 50, 20, 475, 300 };
    chart->scale_width = 100;
    chart->label_height = 20;
    chart->x_count = 366;
    return chart;
}

void test_x_at()
{
    Test::ScopedTestApp app;
    auto                chart = make_chart();
    CHECK_EQ(chart->x_at(0), 100.0f);
    CHECK_EQ(chart->x_at(365), 465.0f);
    CHECK_EQ(chart->x_at(73), 173.0f);
    // Nothing to spread out: all at the left.
    chart->x_count = 1;
    CHECK_EQ(chart->x_at(0), 100.0f);
}

void test_y_at()
{
    Test::ScopedTestApp app;
    auto                chart = make_chart();
    auto const          scale = ChartScale { .top = 100, .step = 20 };
    CHECK_EQ(chart->y_at(0, scale), 270.0f);
    CHECK_EQ(chart->y_at(100, scale), 10.0f);
    CHECK_EQ(chart->y_at(50, scale), 140.0f);
}

// The scale fits the highest value of any line, also of a short one.
void test_scale()
{
    Test::ScopedTestApp app;
    auto                chart = make_chart();
    CHECK((chart->scale() == ChartScale { .top = 1, .step = 1 }));
    chart->series = {
        { .label = "2025", .values = { 0, 10, 40 } },
        { .label = "2026", .bold = true, .values = { 0, 95 } },
        { .label = "2024" },
    };
    CHECK((chart->scale() == ChartScale { .top = 100, .step = 20 }));
    chart->min_step = 50;
    CHECK((chart->scale() == ChartScale { .top = 100, .step = 50 }));
}

// A line below 0, as a balance can go: the scale goes down to it.
void test_scale_below_zero()
{
    Test::ScopedTestApp app;
    auto                chart = make_chart();
    chart->series = {
        { .label = "CTL", .bold = true, .values = { 0, 40, 72 } },
        { .label = "TSB", .values = { 0, -13, -23 } },
    };
    CHECK((chart->scale() == ChartScale { .bottom = -40, .top = 80, .step = 20 }));
    auto const scale = ChartScale { .bottom = -50, .top = 50, .step = 25 };
    CHECK_EQ(chart->y_at(-50, scale), 270.0f);
    CHECK_EQ(chart->y_at(0, scale), 140.0f);
    CHECK_EQ(chart->y_at(50, scale), 10.0f);
}

// A pace's scale: around the values, in steps of time.
void test_nice_span()
{
    std::vector<double> const steps { 5, 10, 15, 30, 60, 120, 300 };
    // 3:20 to 6:10: a minute a step, from 3:00 to 7:00.
    CHECK((nice_span(200, 370, 5, steps) == ChartScale { .bottom = 180, .top = 420, .step = 60 }));
    // 4:02 to 4:38: 10 s, as 4 steps fit.
    CHECK((nice_span(242, 278, 5, steps) == ChartScale { .bottom = 240, .top = 280, .step = 10 }));
    // 4:02 to 4:52: 15 s.
    CHECK((nice_span(242, 292, 5, steps) == ChartScale { .bottom = 240, .top = 300, .step = 15 }));
    // A flat line, at 4:00 exactly: a step above it.
    CHECK((nice_span(240, 240, 5, steps) == ChartScale { .bottom = 240, .top = 245, .step = 5 }));
    // Wider than the largest step can make 5 of: the largest.
    CHECK((nice_span(0, 3000, 5, steps) == ChartScale { .bottom = 0, .top = 3000, .step = 300 }));
    CHECK((nice_span(0, 85, 5, { }) == nice_range(0, 85)));
}

void test_scale_with_steps()
{
    Test::ScopedTestApp app;
    auto                chart = make_chart();
    chart->steps = { 5, 10, 15, 30, 60 };
    CHECK((chart->scale() == ChartScale { .top = 1, .step = 1 }));
    chart->series = {
        { .label = "All time", .values = { 230, 250, 300 } },
        { .label = "90 days", .bold = true, .values = { 245, 262 } },
    };
    CHECK((chart->scale() == ChartScale { .bottom = 225, .top = 300, .step = 15 }));
}

void test_nice_range()
{
    CHECK((nice_range(0, 85) == nice_scale(85)));
    CHECK((nice_range(-23, 85) == ChartScale { .bottom = -50, .top = 100, .step = 50 }));
    CHECK((nice_range(-23, -5) == ChartScale { .bottom = -25, .top = 0, .step = 5 }));
}

// Fractions, and a count's steps of at least 1.
void test_nice_range_in_fractions()
{
    auto const near = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    auto const tenths = nice_range(-0.3, 0.7);
    CHECK(near(tenths.step, 0.2));
    CHECK(near(tenths.bottom, -0.4));
    CHECK(near(tenths.top, 0.8));
    CHECK(tenths.bottom <= -0.3 && tenths.top >= 0.7);
    // A multiple of the step on 0, so that the zero line is a tick.
    auto const ticks = (0 - tenths.bottom) / tenths.step;
    CHECK(near(ticks, std::round(ticks)));
    auto const count = nice_range(-2, 3, 5, 1);
    CHECK((count == ChartScale { .bottom = -2, .top = 3, .step = 1 }));
    auto const below = nice_range(-0.001, 100);
    CHECK(below.bottom < 0 && below.top == 100);
}

}

int main()
{
    test_x_at();
    test_y_at();
    test_scale();
    test_scale_below_zero();
    test_nice_range();
    test_nice_span();
    test_scale_with_steps();
    test_nice_range_in_fractions();
    return Test::test_result();
}
