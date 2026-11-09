/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cmath>
#include <cwctype>

#include <raylib.h>

#include <Utf8.h>

#include <TextArea.h>

namespace ST {

namespace {

#ifdef __APPLE__
constexpr KeyboardModifier PasteModifier = KModSuper;
constexpr KeyboardModifier WordModifier = KModAlt;
#else
constexpr KeyboardModifier PasteModifier = KModControl;
constexpr KeyboardModifier WordModifier = KModControl;
#endif

bool is_space(rune ch)
{
    return std::iswspace(static_cast<wint_t>(ch)) != 0;
}

}

void TextArea::set(std::string_view const &utf8)
{
    text.clear();
    cursor = 0;
    paste(utf8);
    // Also for text that isn't UTF-8, which pastes nothing.
    changed();
}

std::string TextArea::utf8() const
{
    return to_utf8(text).value_or(std::string { });
}

void TextArea::set_width(float width, Measure measure)
{
    m_width = width;
    m_measure = std::move(measure);
    wrap();
    scroll_to_cursor();
}

bool TextArea::insert(rune ch)
{
    if (!put(ch)) {
        return false;
    }
    changed();
    return true;
}

// Puts a character at the cursor, as insert() does, but without wrapping
// again: a paste wraps once, after its last.
bool TextArea::put(rune ch)
{
    auto const is_codepoint = ch >= 0 && ch <= 0x10FFFF && (ch < 0xD800 || ch > 0xDFFF);
    if (ch == L'\t') {
        ch = L' ';
    }
    if (!is_codepoint || (ch != L'\n' && std::iswcntrl(static_cast<wint_t>(ch)))) {
        return false;
    }
    text.insert(cursor, 1, ch);
    ++cursor;
    return true;
}

void TextArea::newline()
{
    insert(L'\n');
}

void TextArea::paste(std::string_view const &utf8)
{
    auto const pasted = to_wstring(utf8);
    if (!pasted) {
        return;
    }
    for (auto ix = 0uz; ix < pasted->length(); ++ix) {
        auto ch = (*pasted)[ix];
        if (ch == L'\r') {
            // \r\n is one line break, and so is \r by itself.
            if (ix + 1 < pasted->length() && (*pasted)[ix + 1] == L'\n') {
                continue;
            }
            ch = L'\n';
        }
        put(ch);
    }
    changed();
}

void TextArea::backspace()
{
    if (cursor > 0) {
        text.erase(cursor - 1, 1);
        --cursor;
        changed();
    }
}

void TextArea::del()
{
    if (cursor < text.length()) {
        text.erase(cursor, 1);
        changed();
    }
}

void TextArea::delete_word()
{
    auto const end = cursor;
    word_left();
    if (cursor < end) {
        text.erase(cursor, end - cursor);
        changed();
    }
}

// Left and Right step over the spaces a line wraps after, from the end of
// the one line to the start of the next.
void TextArea::left()
{
    auto const line = line_of(cursor);
    if (line > 0 && cursor == lines[line].start && wraps(line - 1)) {
        cursor = last_position(line - 1);
    } else if (cursor > 0) {
        --cursor;
    }
    moved();
}

void TextArea::right()
{
    auto const line = line_of(cursor);
    if (wraps(line) && cursor == last_position(line)) {
        cursor = lines[line].end;
    } else if (cursor < text.length()) {
        ++cursor;
    }
    moved();
}

void TextArea::word_left()
{
    while (cursor > 0 && is_space(text[cursor - 1])) {
        --cursor;
    }
    while (cursor > 0 && !is_space(text[cursor - 1])) {
        --cursor;
    }
    moved();
}

void TextArea::word_right()
{
    while (cursor < text.length() && is_space(text[cursor])) {
        ++cursor;
    }
    while (cursor < text.length() && !is_space(text[cursor])) {
        ++cursor;
    }
    moved();
}

void TextArea::up()
{
    auto const line = line_of(cursor);
    if (line == 0) {
        cursor = 0;
        moved();
        return;
    }
    auto const goal = m_goal_x.value_or(x_of(cursor));
    cursor = position_at(line - 1, goal);
    m_goal_x = goal;
    scroll_to_cursor();
}

void TextArea::down()
{
    auto const line = line_of(cursor);
    if (line + 1 >= lines.size()) {
        cursor = text.length();
        moved();
        return;
    }
    auto const goal = m_goal_x.value_or(x_of(cursor));
    cursor = position_at(line + 1, goal);
    m_goal_x = goal;
    scroll_to_cursor();
}

void TextArea::home()
{
    cursor = lines[line_of(cursor)].start;
    moved();
}

void TextArea::end()
{
    cursor = last_position(line_of(cursor));
    moved();
}

void TextArea::text_start()
{
    cursor = 0;
    moved();
}

void TextArea::text_end()
{
    cursor = text.length();
    moved();
}

bool TextArea::handle_key(KeyboardModifier modifier, int key, Clipboard const &clipboard)
{
    if (modifier == PasteModifier && key == KEY_V) {
        if (clipboard) {
            paste(clipboard());
        }
        return true;
    }
#ifdef __APPLE__
    if (modifier == KModSuper && (key == KEY_UP || key == KEY_DOWN)) {
        (key == KEY_UP) ? text_start() : text_end();
        return true;
    }
#else
    if (modifier == KModControl && (key == KEY_HOME || key == KEY_END)) {
        (key == KEY_HOME) ? text_start() : text_end();
        return true;
    }
#endif
    if (modifier == WordModifier) {
        switch (key) {
        case KEY_LEFT:
            word_left();
            return true;
        case KEY_RIGHT:
            word_right();
            return true;
        case KEY_BACKSPACE:
            delete_word();
            return true;
        default:
            return false;
        }
    }
    if (modifier != KModNone) {
        return false;
    }
    switch (key) {
    case KEY_LEFT:
        left();
        return true;
    case KEY_RIGHT:
        right();
        return true;
    case KEY_UP:
        up();
        return true;
    case KEY_DOWN:
        down();
        return true;
    case KEY_HOME:
        home();
        return true;
    case KEY_END:
        end();
        return true;
    case KEY_BACKSPACE:
        backspace();
        return true;
    case KEY_DELETE:
        del();
        return true;
    case KEY_ENTER:
    case KEY_KP_ENTER:
        newline();
        return true;
    default:
        return false;
    }
}

size_t TextArea::line_of(size_t position) const
{
    // The last line that starts at or before it.
    auto const after = std::ranges::upper_bound(lines, position, { }, &Line::start);
    return static_cast<size_t>(std::max(std::ptrdiff_t { 0 }, std::distance(lines.begin(), after) - 1));
}

float TextArea::x_of(size_t position) const
{
    return measure(lines[line_of(position)].start, position);
}

size_t TextArea::position_at(size_t line, float x) const
{
    line = std::min(line, lines.size() - 1);
    auto best = lines[line].start;
    auto best_distance = std::abs(x);
    for (auto position = best + 1; position <= last_position(line); ++position) {
        auto const distance = std::abs(measure(lines[line].start, position) - x);
        if (distance < best_distance) {
            best = position;
            best_distance = distance;
        }
    }
    return best;
}

void TextArea::click(size_t line, float x)
{
    cursor = position_at(line, x);
    moved();
}

void TextArea::scroll_to_cursor()
{
    auto const line = line_of(cursor);
    if (line < top_line) {
        top_line = line;
    } else if (line >= top_line + visible_lines) {
        top_line = line + 1 - visible_lines;
    }
    // Not past the last screenful of lines, as after the box grew.
    scroll(0);
}

void TextArea::scroll(int count)
{
    auto const max_top = static_cast<int>(lines.size()) - static_cast<int>(std::min(visible_lines, lines.size()));
    top_line = static_cast<size_t>(std::clamp(static_cast<int>(top_line) + count, 0, max_top));
}

// After the text changed: wrapped again, the cursor in view, and up and
// down aiming for where it is now.
void TextArea::changed()
{
    wrap();
    moved();
}

void TextArea::moved()
{
    m_goal_x.reset();
    scroll_to_cursor();
}

void TextArea::wrap()
{
    lines.clear();
    size_t start = 0;
    while (true) {
        auto const paragraph_end = std::min(text.find(L'\n', start), text.length());
        do {
            auto const line = wrap_line(start, paragraph_end);
            lines.push_back(line);
            start = line.end;
        } while (start < paragraph_end);
        if (paragraph_end == text.length()) {
            break;
        }
        start = paragraph_end + 1;
    }
    top_line = std::min(top_line, lines.size() - 1);
}

// The line from `start` that fits the width: as many words as fit, each
// with the spaces after it, or as much as fits of a word wider than the
// line by itself, but at least a character.
TextArea::Line TextArea::wrap_line(size_t start, size_t paragraph_end) const
{
    auto end = start;
    while (end < paragraph_end) {
        auto word_end = end;
        while (word_end < paragraph_end && !is_space(text[word_end])) {
            ++word_end;
        }
        if (measure(start, word_end) > m_width) {
            if (end == start) {
                // A word too wide by itself: as much of it as fits.
                auto fits = start + 1;
                while (fits < word_end && measure(start, fits + 1) <= m_width) {
                    ++fits;
                }
                return Line { start, fits };
            }
            break;
        }
        end = word_end;
        while (end < paragraph_end && is_space(text[end])) {
            ++end;
        }
    }
    return Line { start, end };
}

float TextArea::measure(size_t start, size_t end) const
{
    auto const piece = rune_view { text }.substr(start, end - start);
    return m_measure ? m_measure(piece) : static_cast<float>(piece.length());
}

// Whether the line wraps onto the next, rather than ending its paragraph.
bool TextArea::wraps(size_t line) const
{
    return line + 1 < lines.size() && lines[line + 1].start == lines[line].end && lines[line].end > lines[line].start;
}

// The last position on a line: one that wraps after spaces ends before
// them, as a cursor after them would be past the line's width. One that
// breaks a word ends where the next line starts, which is where the word
// goes on.
size_t TextArea::last_position(size_t line) const
{
    auto const &[start, end] = lines[line];
    if (!wraps(line) || !is_space(text[end - 1])) {
        return end;
    }
    auto last = end;
    while (last > start && is_space(text[last - 1])) {
        --last;
    }
    return last;
}

}
