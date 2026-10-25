/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the scrolling ListBox and Table share, with made-up mouse input.
 */

#include <Scroll.h>

#include <TestApp.h>

using namespace ST;

namespace {

// A track 100 high, at x 190, next to rows from x 0 to 190.
constexpr Rectangle Track { .x = 190, .y = 0, .width = 10, .height = 100 };
constexpr Rectangle Content { .x = 0, .y = 0, .width = 190, .height = 100 };

ScrollInput press_at(float y)
{
    return ScrollInput { .mouse = { .x = 195, .y = y }, .pressed = true, .down = true };
}

ScrollInput drag_to(float y)
{
    return ScrollInput { .mouse = { .x = 195, .y = y }, .down = true };
}

ScrollInput wheel(float notches)
{
    return ScrollInput { .mouse = { .x = 50, .y = 50 }, .wheel = notches };
}

void test_clamp()
{
    ScrollRange range { .count = 10, .lines = 4, .top_line = 8 };
    range.clamp();
    CHECK_EQ(range.top_line, 6);
    range.top_line = -3;
    range.clamp();
    CHECK_EQ(range.top_line, 0);
    // Everything fits.
    ScrollRange fits { .count = 3, .lines = 4, .top_line = 2 };
    fits.clamp();
    CHECK_EQ(fits.top_line, 0);
    CHECK(!fits.overflows());
    CHECK(range.overflows());
}

void test_scroll_to()
{
    ScrollRange range { .count = 20, .lines = 5, .top_line = 0 };
    range.scroll_to(3);
    CHECK_EQ(range.top_line, 0);
    // Just far enough: the row at the bottom.
    range.scroll_to(7);
    CHECK_EQ(range.top_line, 3);
    range.scroll_to(1);
    CHECK_EQ(range.top_line, 1);
}

void test_page()
{
    ScrollRange range { .count = 20, .lines = 5, .top_line = 0 };
    range.page(1);
    CHECK_EQ(range.top_line, 5);
    range.page(1);
    range.page(1);
    range.page(1);
    CHECK_EQ(range.top_line, 15);
    range.page(-1);
    CHECK_EQ(range.top_line, 10);
}

void test_thumb()
{
    // A quarter of the rows in view: a quarter of the track.
    ScrollRange range { .count = 20, .lines = 5, .top_line = 0 };
    auto        thumb = Scrollbar::thumb(Track, range);
    CHECK_EQ(thumb.y, 0.0f);
    CHECK_EQ(thumb.height, 25.0f);
    range.top_line = range.max_top_line();
    thumb = Scrollbar::thumb(Track, range);
    CHECK_EQ(thumb.y + thumb.height, 100.0f);
    // A thousand rows: not thinner than the minimum.
    ScrollRange many { .count = 1000, .lines = 5, .top_line = 0 };
    CHECK_EQ(Scrollbar::thumb(Track, many).height, Scrollbar::MinThumbHeight);
}

void test_wheel()
{
    ScrollRange range { .count = 20, .lines = 5, .top_line = 5 };
    Scrollbar   bar;
    // Down, a negative notch, shows the rows below.
    CHECK(bar.handle(range, Track, Content, wheel(-1)));
    CHECK_EQ(range.top_line, 6);
    CHECK(bar.handle(range, Track, Content, wheel(2)));
    CHECK_EQ(range.top_line, 4);
    // Not over the rows or the bar.
    auto elsewhere = wheel(1);
    elsewhere.mouse = { .x = 500, .y = 50 };
    CHECK(!bar.handle(range, Track, Content, elsewhere));
    CHECK_EQ(range.top_line, 4);
}

void test_track_pages()
{
    ScrollRange range { .count = 20, .lines = 5, .top_line = 0 };
    Scrollbar   bar;
    // Below the thumb, which is 25 high at the top.
    CHECK(bar.handle(range, Track, Content, press_at(80)));
    CHECK_EQ(range.top_line, 5);
    CHECK(!bar.dragging);
    // Above it: it's at 5 / 15 of the travel of 75 now, from 25 to 50.
    CHECK(bar.handle(range, Track, Content, press_at(10)));
    CHECK_EQ(range.top_line, 0);
}

void test_drag()
{
    ScrollRange range { .count = 20, .lines = 5, .top_line = 0 };
    Scrollbar   bar;
    // Grabbed 10 below its top.
    CHECK(bar.handle(range, Track, Content, press_at(10)));
    CHECK(bar.dragging);
    // Its top to 75, the end of its travel: the last rows.
    CHECK(bar.handle(range, Track, Content, drag_to(85)));
    CHECK_EQ(range.top_line, 15);
    // Past the end, and back up halfway.
    CHECK(bar.handle(range, Track, Content, drag_to(400)));
    CHECK_EQ(range.top_line, 15);
    CHECK(bar.handle(range, Track, Content, drag_to(47.5f)));
    CHECK_EQ(range.top_line, 8);
    // Let go: the next press elsewhere isn't the bar's.
    CHECK(bar.handle(range, Track, Content, ScrollInput { .mouse = { .x = 195, .y = 47.5f } }));
    CHECK(!bar.dragging);
    auto on_a_row = press_at(50);
    on_a_row.mouse.x = 50;
    CHECK(!bar.handle(range, Track, Content, on_a_row));
}

// A trackpad, as on a MacBook, reports the wheel in fractions of a notch,
// a little each frame. They add up: four quarters scroll a row.
void test_trackpad_wheel()
{
    ScrollRange range { .count = 20, .lines = 5, .top_line = 5 };
    Scrollbar   bar;
    for (auto frame = 0; frame < 4; ++frame) {
        bar.handle(range, Track, Content, wheel(-0.25f));
    }
    CHECK_EQ(range.top_line, 6);
}

void test_nothing_when_everything_fits()
{
    ScrollRange range { .count = 3, .lines = 5, .top_line = 0 };
    Scrollbar   bar;
    CHECK(!bar.handle(range, Track, Content, press_at(50)));
    CHECK(!bar.handle(range, Track, Content, wheel(-1)));
    CHECK_EQ(range.top_line, 0);
}

}

int main()
{
    test_clamp();
    test_scroll_to();
    test_page();
    test_thumb();
    test_wheel();
    test_track_pages();
    test_drag();
    test_trackpad_wheel();
    test_nothing_when_everything_fits();
    return ST::Test::test_result();
}
