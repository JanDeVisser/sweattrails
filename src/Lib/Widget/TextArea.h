/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Editing a text of several lines, such as an activity's description: the
 * text, the cursor, the lines as wrapped, and the keys that move the
 * cursor and change the text. No drawing, so that tests can type into it,
 * with a made-up measure of the text's width.
 */

#pragma once

#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Widget.h>

namespace ST {

struct TextArea {
    // What a paste pastes, as UTF-8: GetClipboardText() in the app.
    using Clipboard = std::function<std::string()>;
    // How wide a piece of the text is drawn, as the font measures it.
    using Measure = std::function<float(rune_view)>;

    // A line as wrapped: text[start, end). It doesn't include the line
    // break that ends a paragraph, and does include the spaces a line
    // was wrapped after.
    struct Line {
        size_t start;
        size_t end;

        bool operator==(Line const &) const = default;
    };

    rune_string       text { };
    size_t            cursor { 0 }; // Before text[cursor]
    std::vector<Line> lines { { 0, 0 } };
    size_t            top_line { 0 };      // The first line in view
    size_t            visible_lines { 1 }; // How many lines are in view

    // The text, with the cursor at its end, scrolled to it.
    void                      set(std::string_view const &utf8);
    [[nodiscard]] std::string utf8() const;

    // Wraps the lines `width` wide, as `measure` measures them. Without
    // one, each character is 1 wide, and lines are as long as they are.
    void set_width(float width, Measure measure);

    // Types a character at the cursor. A tab is a space; other control
    // characters, but for a line break, aren't typed, nor a rune that
    // isn't a Unicode character. Returns whether it was.
    bool insert(rune ch);
    void newline();
    // Text at the cursor, with its line breaks, \r\n and \r too.
    void paste(std::string_view const &utf8);
    void backspace();
    void del();
    // Deletes back to the start of the word before the cursor.
    void delete_word();

    void left();
    void right();
    // A word at a time: to the start of the word before the cursor, or
    // past the end of the one after it.
    void word_left();
    void word_right();
    // A line up or down, as wrapped, at the x the moves up and down
    // started from, so that a short line in between doesn't pull the
    // cursor to the left. Up on the first line goes to the start, and
    // down on the last line to the end.
    void up();
    void down();
    // The start and end of the line, as wrapped.
    void home();
    void end();
    // The start and end of the text.
    void text_start();
    void text_end();

    // Typing keys, with the modifiers of the platform: on macOS, Alt for
    // words and Cmd+Up and Cmd+Down for the text's start and end, and
    // elsewhere Ctrl and Ctrl+Home and Ctrl+End. Pastes with Cmd+V on
    // macOS and Ctrl+V elsewhere. Enter is a line break. Returns whether
    // it took the key.
    bool handle_key(KeyboardModifier modifier, int key, Clipboard const &clipboard);

    // The line the cursor, or any position in the text, is on: a
    // position where a line wraps is at the start of the next one.
    [[nodiscard]] size_t line_of(size_t position) const;
    // How far from the start of its line a position is.
    [[nodiscard]] float x_of(size_t position) const;
    // The position on `line` nearest to `x`, as for a click: never past
    // the spaces it wraps after, which would put it on the next line.
    [[nodiscard]] size_t position_at(size_t line, float x) const;

    // Scrolls just far enough to have the cursor's line in view, with no
    // empty lines below the last.
    void scroll_to_cursor();
    // Scrolls `count` lines down, or up for a negative count, as far as
    // there are lines.
    void scroll(int count);

private:
    float                m_width { std::numeric_limits<float>::max() };
    Measure              m_measure { };
    std::optional<float> m_goal_x { }; // Where up() and down() aim for
    void                 changed();
    void                 wrap();
    [[nodiscard]] float  measure(size_t start, size_t end) const;
    [[nodiscard]] size_t last_position(size_t line) const;
    [[nodiscard]] Line   wrap_line(size_t start, size_t paragraph_end) const;
    void                 moved();
};

}
