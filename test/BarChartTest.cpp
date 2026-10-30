/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests BarChart's scale, which bar is where, and which labels fit.
 * draw() needs fonts, so the tests set the room for the scale and the
 * labels themselves.
 */

#include <cmath>
#include <memory>

#include <BarChart.h>

#include <TestApp.h>

using namespace ST;

namespace {

void test_nice_scale()
{
    CHECK((nice_scale(100) == ChartScale { .top = 100, .step = 20 }));
    CHECK((nice_scale(47) == ChartScale { .top = 50, .step = 10 }));
    CHECK((nice_scale(3120) == ChartScale { .top = 4000, .step = 1000 }));
    CHECK((nice_scale(1) == ChartScale { .top = 1, .step = 0.2 }));
    auto const small = nice_scale(0.3);
    CHECK(std::abs(small.top - 0.3) < 1e-9 && std::abs(small.step - 0.1) < 1e-9);
    // Nothing to show: a scale up to 1 all the same.
    CHECK((nice_scale(0) == ChartScale { .top = 1, .step = 1 }));
}

// A count has no steps smaller than 1.
void test_nice_scale_of_a_count()
{
    CHECK((nice_scale(3, 5, 1) == ChartScale { .top = 3, .step = 1 }));
    CHECK((nice_scale(12, 5, 1) == ChartScale { .top = 15, .step = 5 }));
    CHECK((nice_scale(0, 5, 1) == ChartScale { .top = 1, .step = 1 }));
}

// Ten bars 40 wide: the scale takes the first 100 of the chart's 510, and
// a margin of 10 is on the right.
std::shared_ptr<BarChart> make_chart()
{
    auto chart = std::make_shared<BarChart>(nullptr);
    chart->viewport = Rect<float> { 50, 20, 510, 300 };
    chart->scale_width = 100;
    chart->label_height = 20;
    std::vector<Bar> bars;
    for (auto ix = 0; ix < 10; ++ix) {
        bars.push_back(Bar { .label = "", .value = static_cast<double>(ix) });
    }
    chart->set_bars(bars);
    return chart;
}

int bar(std::optional<size_t> ix)
{
    return ix ? static_cast<int>(*ix) : -1;
}

void test_bar_at()
{
    Test::ScopedTestApp app;
    auto                chart = make_chart();
    CHECK_EQ(bar(chart->bar_at(99)), -1); // On the scale
    CHECK_EQ(bar(chart->bar_at(100)), 0);
    CHECK_EQ(bar(chart->bar_at(139)), 0);
    CHECK_EQ(bar(chart->bar_at(140)), 1);
    CHECK_EQ(bar(chart->bar_at(499)), 9);
    CHECK_EQ(bar(chart->bar_at(500)), -1); // In the margin
    chart->set_bars({ });
    CHECK_EQ(bar(chart->bar_at(200)), -1);
}

void test_highlight_out_of_range()
{
    Test::ScopedTestApp app;
    auto                chart = make_chart();
    chart->highlight = 9;
    chart->set_bars({ Bar { .value = 1 } });
    CHECK(!chart->highlight);
}

void test_scale_of_the_bars()
{
    Test::ScopedTestApp app;
    auto                chart = make_chart();
    CHECK((chart->scale() == ChartScale { .top = 10, .step = 2 }));
    chart->set_bars({ });
    CHECK((chart->scale() == ChartScale { .top = 1, .step = 1 }));
}

// Labels 30 wide, 20 apart: every other one fits, and empty ones are
// passed over.
void test_visible_labels()
{
    CHECK((visible_labels({ 10, 30, 50, 70 }, { 30, 30, 30, 30 }, 5) == std::vector<size_t> { 0, 2 }));
    CHECK((visible_labels({ 10, 30, 50, 70 }, { 30, 0, 0, 30 }, 5) == std::vector<size_t> { 0, 3 }));
    CHECK((visible_labels({ 10, 50 }, { 30, 30 }, 5) == std::vector<size_t> { 0, 1 }));
    CHECK(visible_labels({ }, { }, 5).empty());
}

}

int main()
{
    test_nice_scale();
    test_nice_scale_of_a_count();
    test_bar_at();
    test_highlight_out_of_range();
    test_scale_of_the_bars();
    test_visible_labels();
    return Test::test_result();
}
