/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cmath>

#include <Dashes.h>

namespace ST {

std::vector<Dash> dashes(std::span<Vector2 const> points, float dash, float gap)
{
    std::vector<Dash> ret;
    if (dash <= 0 || gap < 0) {
        return ret;
    }
    auto drawing = true;
    auto left = dash; // Of the dash or the gap the pattern is in
    for (auto ix = 1uz; ix < points.size(); ++ix) {
        auto const a = points[ix - 1];
        auto const b = points[ix];
        auto const length = std::hypot(b.x - a.x, b.y - a.y);
        auto const along = [&](float d) {
            return Vector2 { .x = a.x + (b.x - a.x) * d / length, .y = a.y + (b.y - a.y) * d / length };
        };
        // Up to where the dash or gap ends, and then the next, until the
        // rest of the segment is shorter than what's left of the pattern.
        auto done = 0.0f;
        while (length - done > left) {
            if (drawing && left > 0) {
                ret.push_back(Dash { .from = along(done), .to = along(done + left) });
            }
            done += left;
            drawing = !drawing;
            left = drawing ? dash : gap;
        }
        if (drawing && length > done) {
            ret.push_back(Dash { .from = along(done), .to = b });
        }
        left -= length - done;
    }
    return ret;
}

void draw_dashed_line(std::span<Vector2 const> points, float width, float dash, float gap, Color color)
{
    for (auto const &d : dashes(points, dash, gap)) {
        DrawLineEx(d.from, d.to, width, color);
    }
}

}
