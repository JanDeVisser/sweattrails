/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * A row of tabs, the selected one highlighted: `Whole  Swim  [Bike]  Run`.
 * It isn't a widget of its own: the widget it's part of draws it where it
 * goes among its own rows, and asks it which tab a click is on.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <raylib.h>

#include <Widget.h>

namespace ST {

struct TabStrip {
    // Around a label, inside its tab.
    static constexpr float Padding = 6.0f;

    std::vector<std::string> labels { };
    size_t                   selected { 0 };
    // Where the last draw() drew each tab, in screen coordinates.
    std::vector<Rectangle> areas { };

    // The next tab, `by` 1, or the one before, -1, wrapping around.
    void step(int by);
    // The tab at `point`, in screen coordinates, where the last draw()
    // drew them.
    [[nodiscard]] std::optional<size_t> tab_at(Vector2 point) const;
    [[nodiscard]] static float          height(FontSize size);
    // Draws the tabs at `x`, `y` in `on`.
    void draw(Widget const &on, float x, float y, FontSize size);
};

}
