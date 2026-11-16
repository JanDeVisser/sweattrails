/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * One line at the top of a window: a menu button at the left, and next to
 * it a breadcrumb trail, whose crumbs can be clicked.
 */

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <raylib.h>

#include <PopupMenu.h>
#include <Scroll.h>
#include <Widget.h>

namespace ST {

struct TopBar : public Widget {
    std::vector<MenuItem>    menu { };  // What the button opens
    std::vector<std::string> trail { }; // Shown separated by "›"
    // A click on a crumb, by its index in the trail.
    std::function<void(size_t)> on_crumb { };
    // Where draw() drew the button and the crumbs, in screen coordinates.
    Rectangle              button_area { };
    std::vector<Rectangle> crumb_areas { };
    // What the mouse is over, for draw() to light it up.
    bool                  button_hovered { false };
    std::optional<size_t> crumb_hovered { };

    explicit TopBar(pWidget const &parent);
    // Where the menu opens: under the button, at its left edge.
    [[nodiscard]] Vector2 menu_point() const;
    // The crumb at `mouse`, in screen coordinates.
    [[nodiscard]] std::optional<size_t> crumb_at(Vector2 mouse) const;
    [[nodiscard]] bool                  on_button(Vector2 mouse) const;
    void                                draw() override;
    void                                process_input() override;
    // Over the button or a crumb, the pointing hand. A click on the button
    // opens the menu, on a crumb calls on_crumb.
    void handle_input(ScrollInput const &input);

private:
    void draw_button() const;
};

}
