/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests editing a line of text a character at a time, also outside ASCII,
 * and pasting, with a made-up clipboard.
 */

#include <string>
#include <string_view>

#include <TextLine.h>

#include <TestApp.h>

using namespace ST;

namespace {

#ifdef __APPLE__
constexpr KeyboardModifier Paste = KModSuper;
#else
constexpr KeyboardModifier Paste = KModControl;
#endif

void type(TextLine &line, std::wstring_view text)
{
    for (auto ch : text) {
        line.insert(ch);
    }
}

void key(TextLine &line, int key, int times = 1)
{
    for (auto ix = 0; ix < times; ++ix) {
        line.handle_key(KModNone, key, { });
    }
}

void test_typing_outside_ascii()
{
    TextLine line;
    type(line, L"Café ride");
    CHECK_EQ(line.utf8(), "Café ride");
    CHECK_EQ(line.cursor, 9uz);
    // Back over " ride" and the é: one character each.
    key(line, KEY_LEFT, 5);
    key(line, KEY_BACKSPACE);
    CHECK_EQ(line.utf8(), "Caf ride");
    type(line, L"é");
    CHECK_EQ(line.utf8(), "Café ride");
}

void test_set_and_utf8()
{
    TextLine line;
    for (auto const *name : { "Rückweg", "Ålesund", "Ride 🚴 to work", "" }) {
        line.set(name);
        CHECK_EQ(line.utf8(), name);
        CHECK_EQ(line.cursor, line.text.length());
    }
    line.set("Ride 🚴");
    CHECK_EQ(line.text.length(), 6uz);
    key(line, KEY_BACKSPACE);
    CHECK_EQ(line.utf8(), "Ride ");
}

void test_moving_and_deleting()
{
    TextLine line;
    line.set("Rückweg");
    key(line, KEY_HOME);
    CHECK_EQ(line.cursor, 0uz);
    key(line, KEY_LEFT);
    CHECK_EQ(line.cursor, 0uz);
    key(line, KEY_BACKSPACE);
    CHECK_EQ(line.utf8(), "Rückweg");
    key(line, KEY_RIGHT);
    key(line, KEY_DELETE);
    CHECK_EQ(line.utf8(), "Rckweg");
    key(line, KEY_END);
    CHECK_EQ(line.cursor, 6uz);
    key(line, KEY_DELETE);
    key(line, KEY_RIGHT);
    CHECK_EQ(line.utf8(), "Rckweg");
    CHECK_EQ(line.cursor, 6uz);
}

void test_control_characters_are_not_typed()
{
    TextLine line;
    CHECK(!line.insert(L'\t'));
    CHECK(!line.insert(L'\n'));
    CHECK(!line.insert(0x7f));
    CHECK(line.insert(L'a'));
    CHECK_EQ(line.utf8(), "a");
}

// A surrogate, or past the last codepoint, isn't a character: typed in, it
// would make utf8() empty.
void test_runes_that_arent_characters_are_not_typed()
{
    TextLine line;
    line.set("Ride");
    CHECK(!line.insert(0xD800));
    CHECK(!line.insert(0xDFFF));
    CHECK(!line.insert(0x110000));
    CHECK(!line.insert(-1));
    CHECK(line.insert(0x10FFFF - 2));
    CHECK_EQ(line.utf8().substr(0, 4), "Ride");
}

void test_paste()
{
    TextLine line;
    line.set("Ride");
    key(line, KEY_HOME);
    auto const clipboard = []() { return std::string { "Morning\r\nzürich\t" }; };
    CHECK(line.handle_key(Paste, KEY_V, clipboard));
    CHECK_EQ(line.utf8(), "Morning zürich Ride");
    CHECK_EQ(line.cursor, 15uz);
    // Not UTF-8: nothing.
    line.paste("\xff\xfe");
    CHECK_EQ(line.utf8(), "Morning zürich Ride");
    // V with another modifier, or none, isn't a paste.
    auto const other = (Paste == KModSuper) ? KModControl : KModSuper;
    CHECK(!line.handle_key(other, KEY_V, clipboard));
    CHECK(!line.handle_key(KModNone, KEY_V, clipboard));
    CHECK_EQ(line.utf8(), "Morning zürich Ride");
}

void test_keys_it_leaves()
{
    TextLine line;
    CHECK(!line.handle_key(KModNone, KEY_ENTER, { }));
    CHECK(!line.handle_key(KModNone, KEY_ESCAPE, { }));
    CHECK(!line.handle_key(KModShift, KEY_LEFT, { }));
}

void test_scroll_keeps_cursor_in_view()
{
    TextLine line;
    // A box 100 wide, a text 200 wide: the cursor at 60 needs no scrolling.
    CHECK_EQ(line.scroll_for(60, 200, 100), 0.0f);
    // At 150, the text scrolls 50 to the left, and stays there while the
    // cursor is in view.
    CHECK_EQ(line.scroll_for(150, 200, 100), 50.0f);
    CHECK_EQ(line.scroll_for(120, 200, 100), 50.0f);
    // Before the box: back to the cursor.
    CHECK_EQ(line.scroll_for(20, 200, 100), 20.0f);
    CHECK_EQ(line.scroll_for(0, 200, 100), 0.0f);
}

// Backspace at the end of a long text: the text scrolls back, so that the
// box shows as much of it as fits, never the empty space after it.
void test_scroll_back_as_the_text_gets_shorter()
{
    TextLine line;
    CHECK_EQ(line.scroll_for(900, 900, 400), 500.0f);
    CHECK_EQ(line.scroll_for(600, 600, 400), 200.0f);
    CHECK_EQ(line.scroll_for(300, 300, 400), 0.0f);
}

// In the middle of the text, next to a character outside the BMP.
void test_editing_in_the_middle()
{
    TextLine line;
    line.set("a🚴b");
    key(line, KEY_LEFT);
    type(line, L"é");
    CHECK_EQ(line.utf8(), "a🚴éb");
    key(line, KEY_HOME);
    key(line, KEY_RIGHT);
    key(line, KEY_DELETE);
    CHECK_EQ(line.utf8(), "aéb");
    CHECK_EQ(line.cursor, 1uz);
}

// Other control characters in what's pasted are left out, a lone \r is a
// space, and an empty clipboard pastes nothing.
void test_paste_edge_cases()
{
    TextLine line;
    line.paste("a\x01"
               "b\x7f\r");
    CHECK_EQ(line.utf8(), "ab ");
    CHECK_EQ(line.cursor, 3uz);
    CHECK(line.handle_key(Paste, KEY_V, []() { return std::string { }; }));
    CHECK(line.handle_key(Paste, KEY_V, { }));
    CHECK_EQ(line.utf8(), "ab ");
}

// A new text starts unscrolled.
void test_set_resets_the_scroll()
{
    TextLine line;
    line.scroll_for(500, 500, 100);
    line.set("Ride");
    CHECK_EQ(line.scroll_x, 0.0f);
}
}

int main()
{
    test_typing_outside_ascii();
    test_set_and_utf8();
    test_moving_and_deleting();
    test_control_characters_are_not_typed();
    test_runes_that_arent_characters_are_not_typed();
    test_paste();
    test_keys_it_leaves();
    test_scroll_keeps_cursor_in_view();
    test_scroll_back_as_the_text_gets_shorter();
    test_editing_in_the_middle();
    test_paste_edge_cases();
    test_set_resets_the_scroll();
    return ST::Test::test_result();
}
