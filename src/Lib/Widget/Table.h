/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * A table: a header and rows of text, a row of which can be selected, with
 * a scrollbar when there are more rows than fit. It doesn't decide what a
 * click on a row means: on_click does.
 */

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <raylib.h>

#include <Scroll.h>
#include <Widget.h>

namespace ST {

struct TableColumn {
    std::string header;
    bool        right_aligned { false }; // Numbers are
};

// A cell per column.
using TableRow = std::vector<std::string>;

struct Table : public Widget {
    static constexpr float ScrollbarWidth = 10.0f;

    std::vector<TableColumn>    columns { };
    std::vector<TableRow>       rows { };
    std::optional<size_t>       selection { };
    ScrollRange                 scroll { };
    Scrollbar                   scrollbar { };
    std::function<void(size_t)> on_click { };

    // The header's and each row's height, which resize() works out from
    // the font. Tests, which have no fonts, set it themselves.
    float row_height { 0 };

    // The columns' widths, which draw() measures when the rows are new.
    std::vector<float> column_widths { };

    // As high as the header and `visible_rows` rows.
    Table(pWidget const &parent, int visible_rows);

    // New rows, with nothing selected, scrolled to the top.
    void set_rows(std::vector<TableColumn> new_columns, std::vector<TableRow> new_rows);

    // Selects a row, or none, and scrolls it into view.
    void select(std::optional<size_t> row);

    // The row at `y` in the table, if there is one: not in the header, and
    // not past the last row.
    [[nodiscard]] std::optional<size_t> row_at(float y) const;

    // Where the rows and the scrollbar are, in screen coordinates.
    [[nodiscard]] Rectangle rows_area() const;
    [[nodiscard]] Rectangle track() const;

    // A click on a row calls on_click, unless the scrollbar takes it.
    void handle_input(ScrollInput const &input);

    void process_input() override;
    void resize() override;
    void draw() override;

private:
    void measure_columns();
    void draw_row(TableRow const &row, float y, Color color) const;
};

}
