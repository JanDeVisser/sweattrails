/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The records the activity's graph shows: the whole activity, or the part
 * it's zoomed to (specs/39-graph-zoom-and-pace.md), and how x maps to them.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ST {

// The records [first, end). Each takes as much of the graph's width as
// any other: the x axis is the record.
struct GraphView {
    size_t first { 0 };
    size_t end { 0 };

    [[nodiscard]] size_t count() const { return end > first ? end - first : 0; }
    bool                 operator==(GraphView const &) const = default;
};

// Where record `ix` starts in a graph `width` wide showing `view`, from
// its left edge: below 0 or past `width` for a record outside the view.
[[nodiscard]] inline float view_x(GraphView const &view, size_t ix, float width)
{
    if (view.count() == 0) {
        return 0;
    }
    return (static_cast<float>(ix) - static_cast<float>(view.first)) * width / static_cast<float>(view.count());
}

// The record at `x` from the graph's left edge, within the view, also for
// an `x` past either end, such as while dragging.
[[nodiscard]] inline size_t view_record(GraphView const &view, float x, float width)
{
    if (view.count() == 0 || width <= 0) {
        return view.first;
    }
    auto const at = std::floor(x * static_cast<float>(view.count()) / width);
    return view.first + static_cast<size_t>(std::clamp(at, 0.0f, static_cast<float>(view.count() - 1)));
}

}
