/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <App.h>
#include <TabStrip.h>

namespace ST {

namespace {

// As the table shows its selected row.
constexpr Color SelectedBackground = RAYWHITE;
constexpr Color SelectedColor = DARKGRAY;
constexpr Color LabelColor = LIGHTGRAY;

}

void TabStrip::step(int by)
{
    if (labels.empty()) {
        return;
    }
    auto const count = static_cast<int>(labels.size());
    selected = static_cast<size_t>(((static_cast<int>(selected) + by) % count + count) % count);
}

std::optional<size_t> TabStrip::tab_at(Vector2 point) const
{
    for (size_t ix = 0; ix < areas.size(); ++ix) {
        if (CheckCollisionPointRec(point, areas[ix])) {
            return ix;
        }
    }
    return { };
}

float TabStrip::height(FontSize size)
{
    return App::measure_text("Mg", size).y + 2 * Padding;
}

void TabStrip::draw(Widget const &on, float x, float y, FontSize size)
{
    areas.clear();
    auto const tab_height = height(size);
    for (size_t ix = 0; ix < labels.size(); ++ix) {
        auto const      width = App::measure_text(labels[ix].c_str(), size).x + 2 * Padding;
        Rectangle const area { .x = on.viewport.x + x, .y = on.viewport.y + y, .width = width, .height = tab_height };
        if (ix == selected) {
            DrawRectangleRounded(area, 0.3f, 4, SelectedBackground);
        }
        on.render_text(x + Padding, y + Padding, labels[ix], size, ix == selected ? SelectedColor : LabelColor);
        areas.push_back(area);
        x += width + Padding;
    }
}

}
