/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * A menu that pops up at the mouse, such as on a right-click: a column of
 * items to choose one from with the mouse or the keys. An item that can't
 * be chosen now is greyed out.
 */

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <raylib.h>

#include <Modal.h>

namespace ST {

struct MenuItem {
    std::string           title;
    bool                  enabled { true };
    std::function<void()> action { };
};

// Where a menu `size` big goes for a click at `point`: right of and below
// it, or left of or above it where that would go past the screen's edge,
// and never off the screen.
[[nodiscard]] Vector2 menu_position(Vector2 point, Vector2 size, Rectangle const &screen);

struct PopupMenu : public Modal {
    static constexpr float Padding = 6.0f;

    std::vector<MenuItem> items;
    Vector2               point;
    std::optional<size_t> selection { }; // The item Enter chooses: never a greyed-out one
    float                 row_height { 0 };

    PopupMenu(Vector2 point, std::vector<MenuItem> items);
    void initialize() override;
    void resize() override;
    void draw() override;
    // The mouse: the item under it is selected, a click chooses it, and a
    // click outside the menu closes it.
    void process_input() override;
    // The mouse at `mouse`, in screen coordinates, whether it moved, and
    // whether a button was pressed. A press on a greyed-out item or the
    // menu's edge does nothing.
    void handle_mouse(Vector2 mouse, bool moved, bool pressed);
    // Up and Down, skipping greyed-out items, Enter and Esc.
    bool process_key(KeyboardModifier modifier, int key) override;

    // The item at `mouse`, in screen coordinates, if it's one that can be
    // chosen.
    [[nodiscard]] std::optional<size_t> item_at(Vector2 mouse) const;
    // Selects the next item that can be chosen, `step` 1 down or -1 up,
    // staying where it is at either end.
    void select_next(int step);
    // Closes the menu and runs the item's action.
    void choose(size_t ix);
    void close();
};

// Opens a menu at `point`, in screen coordinates, over everything else.
void popup_menu(Vector2 point, std::vector<MenuItem> items);

}
