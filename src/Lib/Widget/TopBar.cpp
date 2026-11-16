/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>

#include <App.h>
#include <TopBar.h>

namespace ST {

namespace {

// Between two crumbs, with room either side.
constexpr char const *Separator = "›";

}

TopBar::TopBar(pWidget const &parent)
    : Widget(parent, SizePolicy::Characters, 1.0f)
{
    background = DARKGRAY;
}

Vector2 TopBar::menu_point() const
{
    return Vector2 { .x = button_area.x, .y = button_area.y + button_area.height };
}

std::optional<size_t> TopBar::crumb_at(Vector2 mouse) const
{
    // The areas are from the last draw(): the trail may be shorter now.
    auto const count = std::min(crumb_areas.size(), trail.size());
    for (auto ix = 0uz; ix < count; ++ix) {
        if (CheckCollisionPointRec(mouse, crumb_areas[ix])) {
            return ix;
        }
    }
    return { };
}

bool TopBar::on_button(Vector2 mouse) const
{
    return CheckCollisionPointRec(mouse, button_area);
}

// Three lines, rather than "☰", which the fonts don't have.
void TopBar::draw_button() const
{
    auto const size = viewport.height;
    if (button_hovered) {
        draw_rectangle_no_normalize(0.0f, 0.0f, size, size, GRAY);
    }
    auto const width = size / 2;
    auto const thickness = std::max(1.0f, size / 12);
    for (auto line = 0; line < 3; ++line) {
        auto const y = size * (0.3f + 0.2f * static_cast<float>(line)) - thickness / 2;
        draw_rectangle_no_normalize((size - width) / 2, y, width, thickness, RAYWHITE);
    }
}

void TopBar::draw()
{
    auto const size = viewport.height;
    button_area = Rectangle { .x = viewport.x, .y = viewport.y, .width = size, .height = size };
    draw_button();

    auto const y = std::max(0.0f, (size - App::the()->char_size(FontSize::Medium).y) / 2);
    auto const separator_width = App::measure_text(Separator, FontSize::Medium).x;
    auto       x = size + PADDING;
    crumb_areas.clear();
    for (auto ix = 0uz; ix < trail.size(); ++ix) {
        if (ix > 0) {
            render_text(x + PADDING, y, Separator, FontSize::Medium, LIGHTGRAY);
            x += separator_width + 2 * PADDING;
        }
        auto const width = App::measure_text(trail[ix].c_str(), FontSize::Medium).x;
        // The crumb's area reaches halfway to the separators, so that
        // there's no gap to miss it in.
        Rectangle const area { .x = viewport.x + x - PADDING, .y = viewport.y, .width = width + 2 * PADDING, .height = size };
        if (crumb_hovered == ix) {
            draw_rectangle_no_normalize(area.x - viewport.x, 0.0f, area.width, size, GRAY);
        }
        render_text(x, y, trail[ix], FontSize::Medium, RAYWHITE);
        crumb_areas.push_back(area);
        x += width;
    }
}

void TopBar::process_input()
{
    handle_input(ScrollInput::current());
}

void TopBar::handle_input(ScrollInput const &input)
{
    button_hovered = on_button(input.mouse);
    crumb_hovered = crumb_at(input.mouse);
    if (!button_hovered && !crumb_hovered) {
        return;
    }
    App::the()->request_cursor(MOUSE_CURSOR_POINTING_HAND);
    if (!input.pressed) {
        return;
    }
    if (button_hovered) {
        if (!menu.empty()) {
            popup_menu(menu_point(), menu);
        }
    } else if (on_crumb) {
        on_crumb(*crumb_hovered);
    }
}

}
