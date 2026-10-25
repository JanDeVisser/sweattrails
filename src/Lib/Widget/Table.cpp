/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cmath>
#include <ranges>

#include <App.h>
#include <Table.h>

namespace ST {

namespace {

constexpr float Margin = 10.0f; // Left of the first column
constexpr Color HeaderColor = LIGHTGRAY;
constexpr Color RowColor = RAYWHITE;
constexpr Color SelectedBackground = RAYWHITE;
constexpr Color SelectedColor = DARKGRAY;

}

Table::Table(pWidget const &parent, int visible_rows)
    : Widget(parent, SizePolicy::Characters, static_cast<float>(visible_rows + 1))
{
    scroll.lines = visible_rows;
}

void Table::set_rows(std::vector<TableColumn> new_columns, std::vector<TableRow> new_rows)
{
    columns = std::move(new_columns);
    rows = std::move(new_rows);
    selection.reset();
    scroll.count = static_cast<int>(rows.size());
    scroll.top_line = 0;
    column_widths.clear();
}

void Table::select(std::optional<size_t> row)
{
    if (row && *row >= rows.size()) {
        row.reset();
    }
    selection = row;
    if (selection) {
        scroll.scroll_to(static_cast<int>(*selection));
    }
}

std::optional<size_t> Table::row_at(float y) const
{
    if (row_height <= 0 || y < row_height) {
        return { };
    }
    auto const line = static_cast<int>(std::floor((y - row_height) / row_height));
    if (line >= scroll.lines) {
        return { };
    }
    auto const row = scroll.top_line + line;
    if (row >= scroll.count) {
        return { };
    }
    return static_cast<size_t>(row);
}

Rectangle Table::rows_area() const
{
    auto const bar_width = scroll.overflows() ? ScrollbarWidth : 0.0f;
    return Rectangle {
        .x = viewport.x,
        .y = viewport.y + row_height,
        .width = viewport.width - bar_width,
        .height = static_cast<float>(scroll.lines) * row_height,
    };
}

Rectangle Table::track() const
{
    auto const rows = rows_area();
    return Rectangle {
        .x = rows.x + rows.width,
        .y = rows.y,
        .width = scroll.overflows() ? ScrollbarWidth : 0.0f,
        .height = rows.height,
    };
}

void Table::handle_input(ScrollInput const &input)
{
    if (scrollbar.handle(scroll, track(), rows_area(), input)) {
        return;
    }
    if (!CheckCollisionPointRec(input.mouse, rows_area())) {
        return;
    }
    auto const row = row_at(input.mouse.y - viewport.y);
    if (!row) {
        return;
    }
    App::the()->request_cursor(MOUSE_CURSOR_POINTING_HAND);
    if (input.pressed && on_click) {
        on_click(*row);
    }
}

void Table::process_input()
{
    handle_input(ScrollInput::current());
}

// As high as the Characters policy makes the header and each row.
void Table::resize()
{
    row_height = 1.2f * App::the()->char_size().y;
}

// Each column is as wide as its header or its widest cell.
void Table::measure_columns()
{
    column_widths.assign(columns.size(), 0.0f);
    auto const widen = [this](size_t column, std::string const &text) {
        column_widths[column] = std::max(column_widths[column], App::measure_text(text.c_str(), FontSize::Medium).x);
    };
    for (auto ix = 0uz; ix < columns.size(); ++ix) {
        widen(ix, columns[ix].header);
    }
    for (auto const &row : rows) {
        for (auto ix = 0uz; ix < std::min(row.size(), columns.size()); ++ix) {
            widen(ix, row[ix]);
        }
    }
}

void Table::draw()
{
    if (columns.empty()) {
        return;
    }
    if (column_widths.size() != columns.size()) {
        measure_columns();
    }
    auto const text_offset = (row_height - App::the()->char_size().y) / 2;
    draw_row(
        columns | std::views::transform(&TableColumn::header) | std::ranges::to<TableRow>(),
        text_offset, HeaderColor);
    draw_line(Margin, row_height - 1, viewport.width - Margin, row_height - 1, GRAY);

    auto const rows_width = rows_area().width;
    for (auto line = 0; line < scroll.lines; ++line) {
        auto const row = static_cast<size_t>(scroll.top_line + line);
        if (row >= rows.size()) {
            break;
        }
        auto const y = row_height * static_cast<float>(line + 1);
        auto       color = RowColor;
        if (row == selection) {
            draw_rectangle(Margin / 2, y, rows_width - Margin, row_height, SelectedBackground);
            color = SelectedColor;
        }
        draw_row(rows[row], y + text_offset, color);
    }
    Scrollbar::draw(track(), scroll);
}

// The cells side by side, a gap of two characters between columns, the
// right-aligned ones flush with their column's right edge.
void Table::draw_row(TableRow const &row, float y, Color color) const
{
    auto const gap = 2 * App::the()->char_size().x;
    auto       x = Margin;
    for (auto ix = 0uz; ix < std::min({ row.size(), columns.size(), column_widths.size() }); ++ix) {
        auto cell_x = x;
        if (columns[ix].right_aligned) {
            cell_x += column_widths[ix] - App::measure_text(row[ix].c_str(), FontSize::Medium).x;
        }
        render_text(cell_x, y, row[ix], FontSize::Medium, color);
        x += column_widths[ix] + gap;
    }
}

}
