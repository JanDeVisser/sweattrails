/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Scrolling through rows, for the widgets that show more rows than fit:
 * ListBox and Table. ScrollRange is which rows are in view; Scrollbar is
 * the bar next to them, and the mouse wheel.
 */

#pragma once

#include <algorithm>

#include <raylib.h>

namespace ST {

// Which of `count` rows are in view: the `lines` rows from `top_line`.
// Ints, like the rows' indices, so that the arithmetic on them can't wrap
// around.
struct ScrollRange {
    int count { 0 };
    int lines { 0 };
    int top_line { 0 };

    // The highest top_line: the last row at the bottom.
    [[nodiscard]] int max_top_line() const
    {
        return std::max(0, count - lines);
    }

    // Whether there are more rows than fit.
    [[nodiscard]] bool overflows() const
    {
        return count > lines;
    }

    void clamp()
    {
        top_line = std::clamp(top_line, 0, max_top_line());
    }

    // Scrolls just far enough to have `row` in view.
    void scroll_to(int row)
    {
        auto const page = std::max(1, lines);
        top_line = std::clamp(top_line, std::max(0, row - page + 1), std::max(0, row));
    }

    void scroll_by(int rows)
    {
        top_line += rows;
        clamp();
    }

    // A page down for a positive `direction`, up for a negative one.
    void page(int direction)
    {
        scroll_by((direction < 0 ? -1 : 1) * std::max(1, lines));
    }
};

// The mouse, as raylib reports it for a frame. A struct of its own, so
// that tests can make up input.
struct ScrollInput {
    Vector2 mouse { };
    bool    pressed { false }; // The left button went down this frame
    bool    down { false };    // The left button is down
    float   wheel { 0 };       // Notches, up positive, as GetMouseWheelMove()

    // This frame's input, from raylib.
    static ScrollInput current();
};

// A vertical scrollbar along a track, for a ScrollRange. It shows, and
// takes input, only when the rows overflow.
struct Scrollbar {
    // The thumb isn't shorter than this, so that it can be grabbed with a
    // thousand rows.
    static constexpr float MinThumbHeight = 12.0f;

    bool  dragging { false };
    float drag_offset { 0 }; // From the thumb's top to where it was grabbed

    // The thumb: as high as the share of rows in view, where top_line is.
    [[nodiscard]] static Rectangle thumb(Rectangle track, ScrollRange const &range);

    // Takes the input for the bar, and the wheel over `content`, the rows,
    // and scrolls `range`. Returns whether it took the input: then a click
    // isn't also a click on a row. Takes nothing when the rows fit.
    bool handle(ScrollRange &range, Rectangle track, Rectangle content, ScrollInput const &input);

    // Draws the track and the thumb, in screen coordinates, when the rows
    // overflow.
    static void draw(Rectangle track, ScrollRange const &range);
};

}
