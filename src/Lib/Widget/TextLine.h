/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Editing a line of text: the text, the cursor, and the keys that move it
 * and change the text. No drawing, so that tests can type into it.
 */

#pragma once

#include <functional>
#include <string>
#include <string_view>

#include <Widget.h>

namespace ST {

// A rune is a character: the cursor moves, and Backspace deletes, one rune
// at a time. That takes 32 bits, as wchar_t is on macOS and Linux.
static_assert(sizeof(rune) == 4, "TextLine needs a rune to be a whole character");

struct TextLine {
    // What a paste pastes, as UTF-8: GetClipboardText() in the app.
    using Clipboard = std::function<std::string()>;

    rune_string text { };
    size_t      cursor { 0 };   // Before text[cursor]
    float       scroll_x { 0 }; // How far the text is scrolled to the left, in pixels

    // The text, with the cursor at its end.
    void                      set(std::string_view const &utf8);
    [[nodiscard]] std::string utf8() const;

    // Types a character at the cursor. Control characters, such as a tab,
    // aren't typed. Returns whether it was.
    bool insert(rune ch);
    // Text at the cursor, with line breaks as spaces: it's one line.
    void paste(std::string_view const &utf8);
    void backspace();
    void del();
    void left();
    void right();
    void home();
    void end();

    // Left, Right, Home, End, Backspace, Delete, and pasting with Cmd+V on
    // macOS and Ctrl+V elsewhere. Returns whether it took the key.
    bool handle_key(KeyboardModifier modifier, int key, Clipboard const &clipboard);

    // Scrolls just far enough to have the cursor in a box `width` wide,
    // given where the cursor is in the whole text, as the font measures
    // it. Returns the new scroll_x.
    float scroll_for(float cursor_x, float width);
};

}
