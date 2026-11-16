/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests what the top bar does with the mouse: which crumb is where, where
 * the menu opens, and what a click calls. draw() needs raylib, so the
 * tests set the areas it would have set by hand. Opening the menu needs
 * fonts too, so the tests don't click the button with a menu.
 */

#include <memory>
#include <vector>

#include <TopBar.h>

#include <TestApp.h>

using namespace ST;

namespace {

// A bar 20 high at the top of the window, with the button and three
// crumbs as draw() would have laid them out.
std::shared_ptr<TopBar> bar_with_trail()
{
    auto bar = std::make_shared<TopBar>(nullptr);
    bar->trail = { "Activities", "2025", "November" };
    bar->button_area = Rectangle { .x = 0, .y = 0, .width = 20, .height = 20 };
    bar->crumb_areas = {
        Rectangle { .x = 25, .y = 0, .width = 90, .height = 20 },
        Rectangle { .x = 130, .y = 0, .width = 50, .height = 20 },
        Rectangle { .x = 195, .y = 0, .width = 90, .height = 20 },
    };
    return bar;
}

void test_crumb_at()
{
    auto const bar = bar_with_trail();
    CHECK(bar->crumb_at({ .x = 50, .y = 10 }) == 0uz);
    CHECK(bar->crumb_at({ .x = 150, .y = 10 }) == 1uz);
    CHECK(bar->crumb_at({ .x = 250, .y = 10 }) == 2uz);
    // On a separator, past the trail, on the button, and under the bar.
    CHECK(!bar->crumb_at({ .x = 120, .y = 10 }));
    CHECK(!bar->crumb_at({ .x = 400, .y = 10 }));
    CHECK(!bar->crumb_at({ .x = 10, .y = 10 }));
    CHECK(!bar->crumb_at({ .x = 50, .y = 40 }));
}

// draw() hasn't run since the trail got shorter, on the totals screen,
// say: the areas left over aren't crumbs.
void test_crumb_at_after_the_trail_shrinks()
{
    auto const bar = bar_with_trail();
    bar->trail = { "Activities" };
    CHECK(bar->crumb_at({ .x = 50, .y = 10 }) == 0uz);
    CHECK(!bar->crumb_at({ .x = 150, .y = 10 }));
    bar->trail.clear();
    CHECK(!bar->crumb_at({ .x = 50, .y = 10 }));
}

void test_button()
{
    auto const bar = bar_with_trail();
    CHECK(bar->on_button({ .x = 10, .y = 10 }));
    CHECK(!bar->on_button({ .x = 50, .y = 10 }));
    auto const point = bar->menu_point();
    CHECK_EQ(point.x, 0.0f);
    CHECK_EQ(point.y, 20.0f);
}

// A click on a crumb calls on_crumb with it; a move, a click between
// crumbs, and a click on the button don't.
void test_click_on_crumb()
{
    auto const          bar = bar_with_trail();
    std::vector<size_t> clicked;
    bar->on_crumb = [&clicked](size_t ix) { clicked.push_back(ix); };
    bar->handle_input(ScrollInput { .mouse = { .x = 150, .y = 10 }, .pressed = true });
    bar->handle_input(ScrollInput { .mouse = { .x = 50, .y = 10 }, .pressed = true });
    bar->handle_input(ScrollInput { .mouse = { .x = 250, .y = 10 } });
    bar->handle_input(ScrollInput { .mouse = { .x = 120, .y = 10 }, .pressed = true });
    bar->handle_input(ScrollInput { .mouse = { .x = 10, .y = 10 }, .pressed = true });
    CHECK_EQ(clicked.size(), 2uz);
    CHECK_EQ(clicked[0], 1uz);
    CHECK_EQ(clicked[1], 0uz);
}

// What draw() lights up, and the pointing hand.
void test_hover()
{
    auto const bar = bar_with_trail();
    bar->handle_input(ScrollInput { .mouse = { .x = 250, .y = 10 } });
    CHECK(bar->crumb_hovered == 2uz);
    CHECK(!bar->button_hovered);
    CHECK(App::the()->m_requested_cursor == MOUSE_CURSOR_POINTING_HAND);
    bar->handle_input(ScrollInput { .mouse = { .x = 10, .y = 10 } });
    CHECK(!bar->crumb_hovered);
    CHECK(bar->button_hovered);
    bar->handle_input(ScrollInput { .mouse = { .x = 400, .y = 10 } });
    CHECK(!bar->crumb_hovered);
    CHECK(!bar->button_hovered);
}

}

int main()
{
    Test::ScopedTestApp app;
    test_crumb_at();
    test_crumb_at_after_the_trail_shrinks();
    test_button();
    test_click_on_crumb();
    test_hover();
    return Test::test_result();
}
