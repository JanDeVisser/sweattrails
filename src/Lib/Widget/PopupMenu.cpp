/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>

#include <PopupMenu.h>

namespace ST {

namespace {

Rectangle rectangle(Rect<float> const &rect)
{
    return Rectangle { .x = rect.x, .y = rect.y, .width = rect.width, .height = rect.height };
}

}

Vector2 menu_position(Vector2 point, Vector2 size, Rectangle const &screen)
{
    auto x = point.x;
    auto y = point.y;
    if (x + size.x > screen.x + screen.width) {
        x = point.x - size.x;
    }
    if (y + size.y > screen.y + screen.height) {
        y = point.y - size.y;
    }
    return Vector2 {
        .x = std::max(screen.x, x),
        .y = std::max(screen.y, y),
    };
}

PopupMenu::PopupMenu(Vector2 point, std::vector<MenuItem> items)
    : Modal("")
    , items(std::move(items))
    , point(point)
{
    background = BLACK;
}

void PopupMenu::initialize()
{
    select_next(1);
    resize();
}

void PopupMenu::resize()
{
    auto const &cell = App::the()->cell();
    row_height = cell.y + 2;
    auto width = 0.0f;
    for (auto const &item : items) {
        width = std::max(width, App::measure_text(item.title.c_str(), FontSize::Medium).x);
    }
    Vector2 const size { .x = width + 4 * Padding, .y = static_cast<float>(items.size()) * row_height + 2 * Padding };
    auto const    at = menu_position(point, size, rectangle(App::the()->viewport));
    viewport.x = at.x;
    viewport.y = at.y;
    viewport.width = size.x;
    viewport.height = size.y;
}

void PopupMenu::draw()
{
    draw_rectangle(0.0f, 0.0f, 0.0f, 0.0f, BLACK);
    draw_outline(1.0f, 1.0f, -1.0f, -1.0f, RAYWHITE);
    auto y = Padding;
    for (auto ix = 0uz; ix < items.size(); ++ix) {
        auto color = items[ix].enabled ? RAYWHITE : GRAY;
        if (selection == ix) {
            draw_rectangle(Padding / 2, y, -Padding / 2, row_height, RAYWHITE);
            color = DARKGRAY;
        }
        render_text(2 * Padding, y + 1, items[ix].title, FontSize::Medium, color);
        y += row_height;
    }
}

std::optional<size_t> PopupMenu::item_at(Vector2 mouse) const
{
    if (!CheckCollisionPointRec(mouse, rectangle(viewport)) || row_height <= 0) {
        return { };
    }
    auto const row = (mouse.y - viewport.y - Padding) / row_height;
    if (row < 0 || row >= static_cast<float>(items.size())) {
        return { };
    }
    auto const ix = static_cast<size_t>(row);
    if (!items[ix].enabled) {
        return { };
    }
    return ix;
}

void PopupMenu::process_input()
{
    auto const mouse = GetMousePosition();
    auto const item = item_at(mouse);
    if (item && (GetMouseDelta().x != 0 || GetMouseDelta().y != 0)) {
        selection = item;
    }
    auto const pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    if (!pressed) {
        return;
    }
    if (item) {
        choose(*item);
    } else if (!CheckCollisionPointRec(mouse, rectangle(viewport))) {
        close();
    }
}

bool PopupMenu::process_key(KeyboardModifier modifier, int key)
{
    if (status == ModalStatus::Dormant || modifier != KModNone) {
        return false;
    }
    switch (key) {
    case KEY_ESCAPE:
        close();
        return true;
    case KEY_ENTER:
    case KEY_KP_ENTER:
        if (selection) {
            choose(*selection);
        }
        return true;
    case KEY_UP:
        select_next(-1);
        return true;
    case KEY_DOWN:
        select_next(1);
        return true;
    default:
        // The menu has the keys while it's open, as any modal does.
        return true;
    }
}

void PopupMenu::select_next(int step)
{
    auto ix = selection ? static_cast<int>(*selection) + step : (step > 0 ? 0 : static_cast<int>(items.size()) - 1);
    for (; ix >= 0 && ix < static_cast<int>(items.size()); ix += step) {
        if (items[static_cast<size_t>(ix)].enabled) {
            selection = static_cast<size_t>(ix);
            return;
        }
    }
}

void PopupMenu::choose(size_t ix)
{
    if (ix >= items.size() || !items[ix].enabled) {
        return;
    }
    // Close first: the action may open a modal of its own.
    auto const action = items[ix].action;
    status = ModalStatus::Submitted;
    App::the()->pop_modal(self());
    if (action) {
        action();
    }
}

void PopupMenu::close()
{
    status = ModalStatus::Dismissed;
    App::the()->pop_modal(self());
}

void popup_menu(Vector2 point, std::vector<MenuItem> items)
{
    auto const menu = Widget::make<PopupMenu>(point, std::move(items));
    menu->make_visible();
}

}
