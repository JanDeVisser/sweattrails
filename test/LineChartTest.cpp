/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests LineChart's scale and where its values go. draw() needs fonts,
 * so the tests set the room for the scale and the labels themselves.
 */

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

}

int main()
{
    test_x_at();
    test_y_at();
    test_scale();
    return Test::test_result();
}
