/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Dashed lines, which raylib doesn't draw.
 */

#pragma once

#include <span>
#include <vector>

#include <raylib.h>

namespace ST {

struct Dash {
    Vector2 from { };
    Vector2 to { };
};

// The dashes along the line through `points`: `dash` long, `gap` apart.
// The pattern carries on from one segment to the next, so that a line of
// many short segments is dashed as a whole. A dash across a corner comes
// in two pieces, one on each side.
[[nodiscard]] std::vector<Dash> dashes(std::span<Vector2 const> points, float dash, float gap);

void draw_dashed_line(std::span<Vector2 const> points, float width, float dash, float gap, Color color);

}
