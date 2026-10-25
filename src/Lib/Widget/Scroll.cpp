/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cmath>

#include <raylib.h>

#include <Scroll.h>

namespace ST {

ScrollInput ScrollInput::current()
{
    return ScrollInput {
        .mouse = GetMousePosition(),
        .pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT),
        .down = IsMouseButtonDown(MOUSE_BUTTON_LEFT),
        .wheel = GetMouseWheelMove(),
    };
}

Rectangle Scrollbar::thumb(Rectangle track, ScrollRange const &range)
{
    if (!range.overflows()) {
        return track;
    }
    auto const share = static_cast<float>(range.lines) / static_cast<float>(range.count);
    auto const height = std::clamp(track.height * share, std::min(MinThumbHeight, track.height), track.height);
    auto const travel = track.height - height;
    auto const at = static_cast<float>(range.top_line) / static_cast<float>(range.max_top_line());
    return Rectangle { .x = track.x, .y = track.y + travel * at, .width = track.width, .height = height };
}

bool Scrollbar::handle(ScrollRange &range, Rectangle track, Rectangle content, ScrollInput const &input)
{
    if (!range.overflows()) {
        dragging = false;
        return false;
    }
    if (dragging) {
        if (!input.down) {
            dragging = false;
            return true;
        }
        // The thumb follows the mouse, and the rows follow the thumb.
        auto const height = thumb(track, range).height;
        auto const travel = track.height - height;
        auto const top = input.mouse.y - drag_offset - track.y;
        range.top_line = (travel > 0) ? static_cast<int>(std::lround(top / travel * static_cast<float>(range.max_top_line()))) : 0;
        range.clamp();
        return true;
    }
    if (input.wheel != 0 && (CheckCollisionPointRec(input.mouse, content) || CheckCollisionPointRec(input.mouse, track))) {
        // Up, a positive notch, shows the rows above. A trackpad reports
        // fractions of a notch: they add up until there's a whole one.
        wheel_total += input.wheel;
        auto const notches = std::trunc(wheel_total);
        wheel_total -= notches;
        range.scroll_by(-static_cast<int>(notches));
        return true;
    }
    if (input.pressed && CheckCollisionPointRec(input.mouse, track)) {
        auto const t = thumb(track, range);
        if (input.mouse.y < t.y) {
            range.page(-1);
        } else if (input.mouse.y >= t.y + t.height) {
            range.page(1);
        } else {
            dragging = true;
            drag_offset = input.mouse.y - t.y;
        }
        return true;
    }
    return false;
}

void Scrollbar::draw(Rectangle track, ScrollRange const &range)
{
    if (!range.overflows()) {
        return;
    }
    DrawRectangleRec(track, Color { 60, 60, 60, 255 });
    DrawRectangleRec(thumb(track, range), LIGHTGRAY);
}

}
