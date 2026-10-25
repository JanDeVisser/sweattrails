/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests Table's selection, scrolling and clicks, with made-up mouse input.
 * resize() and draw() need fonts, so the tests set `row_height` themselves.
 */

#include <format>
#include <memory>
#include <optional>
#include <vector>

#include <Table.h>

#include <TestApp.h>

using namespace ST;

namespace {

constexpr float RowHeight = 20.0f;

// A table of `count` rows, 200 wide at (100, 50): the header from y 50 to
// 70, and five rows from 70 to 170.
std::shared_ptr<Table> make_table(int count)
{
    auto table = Widget::make<Table>(nullptr, 5);
    table->viewport = Rect<float> { 100, 50, 200, 120 };
    table->row_height = RowHeight;
    std::vector<TableRow> rows;
    for (auto ix = 0; ix < count; ++ix) {
        rows.push_back({ std::format("{}", ix + 1), std::format("{} km", ix) });
    }
    table->set_rows({ { .header = "Lap" }, { .header = "Distance", .right_aligned = true } }, rows);
    return table;
}

// A row as an int, -1 for none, for CHECK_EQ: there's no formatter for
// std::optional.
int row(std::optional<size_t> row)
{
    return row ? static_cast<int>(*row) : -1;
}

ScrollInput click_at(float x, float y)
{
    return ScrollInput { .mouse = { .x = x, .y = y }, .pressed = true, .down = true };
}

void test_set_rows()
{
    auto table = make_table(8);
    table->select(6);
    table->set_rows({ { .header = "Lap" } }, { { "1" }, { "2" } });
    CHECK(!table->selection);
    CHECK_EQ(table->scroll.top_line, 0);
    CHECK_EQ(table->scroll.count, 2);
    CHECK(!table->scroll.overflows());
}

void test_select_scrolls()
{
    auto table = make_table(12);
    table->select(7);
    CHECK_EQ(row(table->selection), 7);
    CHECK_EQ(table->scroll.top_line, 3);
    table->select(1);
    CHECK_EQ(table->scroll.top_line, 1);
    // Past the last row: nothing.
    table->select(12);
    CHECK(!table->selection);
    table->select({ });
    CHECK(!table->selection);
}

void test_row_at()
{
    auto table = make_table(12);
    CHECK(!table->row_at(10)); // The header
    CHECK_EQ(row(table->row_at(20)), 0);
    CHECK_EQ(row(table->row_at(119)), 4);
    CHECK(!table->row_at(120)); // Below the fifth row
    table->scroll.top_line = 4;
    CHECK_EQ(row(table->row_at(25)), 4);

    // Fewer rows than fit.
    auto short_table = make_table(2);
    CHECK_EQ(row(short_table->row_at(45)), 1);
    CHECK(!short_table->row_at(65));
}

void test_click()
{
    auto                  table = make_table(12);
    std::optional<size_t> clicked;
    table->on_click = [&clicked](size_t row) { clicked = row; };
    table->scroll.top_line = 2;
    table->handle_input(click_at(150, 95)); // The second row on screen
    CHECK_EQ(row(clicked), 3);
    // The click doesn't select: on_click decides what it means.
    CHECK(!table->selection);

    // Hovering, and clicking on the header, aren't clicks on a row.
    clicked.reset();
    table->handle_input(ScrollInput { .mouse = { .x = 150, .y = 95 } });
    table->handle_input(click_at(150, 60));
    CHECK(!clicked);
}

void test_click_on_scrollbar()
{
    auto                  table = make_table(12);
    std::optional<size_t> clicked;
    table->on_click = [&clicked](size_t row) { clicked = row; };
    auto const track = table->track();
    CHECK_EQ(track.x, 290.0f);
    CHECK_EQ(track.width, Table::ScrollbarWidth);
    // Below the thumb: a page down, and not a click on a row.
    table->handle_input(click_at(295, 160));
    CHECK(!clicked);
    CHECK_EQ(table->scroll.top_line, 5);
}

void test_wheel()
{
    auto table = make_table(12);
    table->select(0);
    table->handle_input(ScrollInput { .mouse = { .x = 150, .y = 100 }, .wheel = -2 });
    CHECK_EQ(table->scroll.top_line, 2);
    CHECK_EQ(row(table->selection), 0);
}

void test_no_scrollbar_when_rows_fit()
{
    auto                  table = make_table(3);
    std::optional<size_t> clicked;
    table->on_click = [&clicked](size_t row) { clicked = row; };
    CHECK_EQ(table->track().width, 0.0f);
    CHECK_EQ(table->rows_area().width, 200.0f);
    // At the right edge, where the scrollbar would be: still the row.
    table->handle_input(click_at(295, 75));
    CHECK_EQ(row(clicked), 0);
}

}

int main()
{
    Test::ScopedTestApp app;
    test_set_rows();
    test_select_scrolls();
    test_row_at();
    test_click();
    test_click_on_scrollbar();
    test_wheel();
    test_no_scrollbar_when_rows_fit();
    return Test::test_result();
}
