/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cwctype>

#include <raylib.h>

#include <Utf8.h>

#include <TextLine.h>

namespace ST {

namespace {

#ifdef __APPLE__
constexpr KeyboardModifier PasteModifier = KModSuper;
#else
constexpr KeyboardModifier PasteModifier = KModControl;
#endif

}

void TextLine::set(std::string_view const &utf8)
{
    text = to_wstring(utf8).value_or(rune_string { });
    cursor = text.length();
    scroll_x = 0;
}

std::string TextLine::utf8() const
{
    return to_utf8(text).value_or(std::string { });
}

bool TextLine::insert(rune ch)
{
    if (std::iswcntrl(static_cast<wint_t>(ch))) {
        return false;
    }
    text.insert(cursor, 1, ch);
    ++cursor;
    return true;
}

void TextLine::paste(std::string_view const &utf8)
{
    auto const pasted = to_wstring(utf8);
    if (!pasted) {
        return;
    }
    for (auto ix = 0uz; ix < pasted->length(); ++ix) {
        auto ch = (*pasted)[ix];
        // A line break of \r\n is one space.
        if (ch == L'\r' && ix + 1 < pasted->length() && (*pasted)[ix + 1] == L'\n') {
            continue;
        }
        if (ch == L'\n' || ch == L'\r' || ch == L'\t') {
            ch = L' ';
        }
        insert(ch);
    }
}

void TextLine::backspace()
{
    if (cursor > 0) {
        text.erase(cursor - 1, 1);
        --cursor;
    }
}

void TextLine::del()
{
    if (cursor < text.length()) {
        text.erase(cursor, 1);
    }
}

void TextLine::left()
{
    if (cursor > 0) {
        --cursor;
    }
}

void TextLine::right()
{
    if (cursor < text.length()) {
        ++cursor;
    }
}

void TextLine::home()
{
    cursor = 0;
}

void TextLine::end()
{
    cursor = text.length();
}

bool TextLine::handle_key(KeyboardModifier modifier, int key, Clipboard const &clipboard)
{
    if (modifier == PasteModifier && key == KEY_V) {
        if (clipboard) {
            paste(clipboard());
        }
        return true;
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
    default:
        return false;
    }
}

float TextLine::scroll_for(float cursor_x, float text_width, float width)
{
    if (cursor_x < scroll_x) {
        scroll_x = cursor_x;
    } else if (cursor_x > scroll_x + width) {
        scroll_x = cursor_x - width;
    }
    scroll_x = std::clamp(scroll_x, 0.0f, std::max(0.0f, text_width - width));
    return scroll_x;
}

}
