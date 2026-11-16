/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <TabStrip.h>

#include <TestApp.h>

using namespace ST;
using namespace ST::Test;

namespace {

TabStrip whole_swim_bike_run()
{
    return TabStrip { .labels = { "Whole", "Swim", "Bike", "Run" } };
}

void test_step_wraps_around()
{
    auto tabs = whole_swim_bike_run();
    tabs.step(1);
    CHECK_EQ(tabs.selected, 1uz);
    tabs.selected = 3;
    tabs.step(1);
    CHECK_EQ(tabs.selected, 0uz);
    tabs.step(-1);
    CHECK_EQ(tabs.selected, 3uz);
}

void test_step_without_tabs()
{
    TabStrip tabs;
    tabs.step(1);
    tabs.step(-1);
    CHECK_EQ(tabs.selected, 0uz);
}

// draw() sets the areas, which needs fonts: a test sets them by hand.
void test_tab_at()
{
    auto tabs = whole_swim_bike_run();
    tabs.areas = {
        Rectangle { .x = 10, .y = 100, .width = 60, .height = 20 },
        Rectangle { .x = 76, .y = 100, .width = 50, .height = 20 },
        Rectangle { .x = 132, .y = 100, .width = 40, .height = 20 },
        Rectangle { .x = 178, .y = 100, .width = 40, .height = 20 },
    };
    CHECK(tabs.tab_at(Vector2 { 20, 110 }) == std::optional<size_t> { 0 });
    CHECK(tabs.tab_at(Vector2 { 150, 105 }) == std::optional<size_t> { 2 });
    // Between two tabs, and below them.
    CHECK(!tabs.tab_at(Vector2 { 73, 110 }));
    CHECK(!tabs.tab_at(Vector2 { 20, 130 }));
}

}

int main()
{
    ScopedTestApp app;
    test_step_wraps_around();
    test_step_without_tabs();
    test_tab_at();
    return test_result();
}
