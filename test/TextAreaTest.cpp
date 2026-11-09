/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests editing a text of several lines: typing, wrapping, moving the
 * cursor by character, word and line, clicks and scrolling, with a font
 * that's 10 px a character, and a made-up clipboard.
 */

#include <string>
#include <string_view>
#include <vector>

#include <Emoji.h>
#include <TextArea.h>

#include <TestApp.h>

using namespace ST;

namespace {

#ifdef __APPLE__
constexpr KeyboardModifier Paste = KModSuper;
constexpr KeyboardModifier Word = KModAlt;
#else
constexpr KeyboardModifier Paste = KModControl;
constexpr KeyboardModifier Word = KModControl;
#endif

using Line = TextArea::Line;

// 10 px a character, as wide as `chars` characters.
TextArea area(std::string_view text, int chars = 1000)
{
    TextArea ret;
    ret.set_width(10.0f * static_cast<float>(chars), [](rune_view piece) { return 10.0f * static_cast<float>(piece.length()); });
    ret.set(text);
    return ret;
}

void type(TextArea &a, std::wstring_view text)
{
    for (auto ch : text) {
        a.insert(ch);
    }
}

void key(TextArea &a, int key, int times = 1, KeyboardModifier modifier = KModNone)
{
    for (auto ix = 0; ix < times; ++ix) {
        a.handle_key(modifier, key, { });
    }
}

void test_typing()
{
    auto a = area("");
    type(a, L"Windy");
    key(a, KEY_ENTER);
    type(a, L"Café");
    CHECK_EQ(a.utf8(), "Windy\nCafé");
    CHECK_EQ(a.cursor, 10uz);
    // A tab is a space, and other control characters aren't typed.
    CHECK(a.insert(L'\t'));
    CHECK(!a.insert(L'\x07'));
    CHECK_EQ(a.utf8(), "Windy\nCafé ");
}

// Backspace and Delete take out a line break as any other character.
void test_deleting_across_a_line_break()
{
    auto a = area("ab\ncd");
    a.cursor = 3;
    key(a, KEY_BACKSPACE);
    CHECK_EQ(a.utf8(), "abcd");
    CHECK_EQ(a.cursor, 2uz);
    a.set("ab\ncd");
    a.cursor = 2;
    key(a, KEY_DELETE);
    CHECK_EQ(a.utf8(), "abcd");
    CHECK(a.lines == (std::vector<Line> { { 0, 4 } }));
}

void test_wrapping()
{
    // 10 characters wide: "the quick " fits, with its space; "brown fox".
    auto a = area("the quick brown fox", 10);
    CHECK(a.lines == (std::vector<Line> { { 0, 10 }, { 10, 19 } }));
    // A word wider than the line is broken where the line is full, and
    // the next line goes on from there.
    auto const long_word = area("abcdefghijklmnop qrstu", 10);
    CHECK(long_word.lines == (std::vector<Line> { { 0, 10 }, { 10, 17 }, { 17, 22 } }));
    // Line breaks, an empty line between, and one after the last.
    auto const breaks = area("ab\n\ncd\n");
    CHECK(breaks.lines == (std::vector<Line> { { 0, 2 }, { 3, 3 }, { 4, 6 }, { 7, 7 } }));
    // Nothing is one empty line.
    CHECK(area("").lines == (std::vector<Line> { { 0, 0 } }));
}

// Where a line wraps is the start of the next one, and End stops before
// the space it wraps after.
void test_line_of_and_end()
{
    auto a = area("the quick brown fox", 10);
    CHECK_EQ(a.line_of(9), 0uz);
    CHECK_EQ(a.line_of(10), 1uz);
    CHECK_EQ(a.line_of(19), 1uz);
    a.cursor = 2;
    key(a, KEY_END);
    CHECK_EQ(a.cursor, 9uz);
    key(a, KEY_RIGHT, 2);
    key(a, KEY_HOME);
    CHECK_EQ(a.cursor, 10uz);
    key(a, KEY_END);
    CHECK_EQ(a.cursor, 19uz);
}

void test_left_and_right()
{
    auto a = area("ab\ncd");
    a.cursor = 0;
    key(a, KEY_LEFT);
    CHECK_EQ(a.cursor, 0uz);
    key(a, KEY_RIGHT, 3);
    CHECK_EQ(a.cursor, 3uz);
    CHECK_EQ(a.line_of(a.cursor), 1uz);
    key(a, KEY_RIGHT, 5);
    CHECK_EQ(a.cursor, 5uz);
}

// Up and Down go to the nearest place on the line, and keep aiming for
// where they started over a shorter line.
void test_up_and_down()
{
    auto a = area("abcdefgh\nab\nabcdefgh");
    a.cursor = 6; // After "abcdef"
    key(a, KEY_DOWN);
    CHECK_EQ(a.cursor, 11uz); // The end of "ab"
    key(a, KEY_DOWN);
    CHECK_EQ(a.cursor, 18uz); // After "abcdef" again
    key(a, KEY_UP, 2);
    CHECK_EQ(a.cursor, 6uz);
    // Up on the first line goes to the start, Down on the last to the end.
    key(a, KEY_UP);
    CHECK_EQ(a.cursor, 0uz);
    a.cursor = 14;
    key(a, KEY_DOWN);
    CHECK_EQ(a.cursor, 20uz);
    // Over wrapped lines too: from "the qu|ick" down to "brown |fox".
    auto w = area("the quick brown fox", 10);
    w.cursor = 6;
    key(w, KEY_DOWN);
    CHECK_EQ(w.cursor, 16uz);
    key(w, KEY_UP);
    CHECK_EQ(w.cursor, 6uz);
}

// Typing between Up and Down aims from where the cursor is then.
void test_typing_forgets_the_goal()
{
    auto a = area("abcdefgh\nab\nabcdefgh");
    a.cursor = 6;
    key(a, KEY_DOWN);
    type(a, L"c");
    key(a, KEY_DOWN);
    CHECK_EQ(a.cursor, 16uz); // After "abc"
}

void test_text_start_and_end()
{
    auto a = area("ab\ncd\nef");
    a.cursor = 4;
#ifdef __APPLE__
    key(a, KEY_UP, 1, KModSuper);
    CHECK_EQ(a.cursor, 0uz);
    key(a, KEY_DOWN, 1, KModSuper);
#else
    key(a, KEY_HOME, 1, KModControl);
    CHECK_EQ(a.cursor, 0uz);
    key(a, KEY_END, 1, KModControl);
#endif
    CHECK_EQ(a.cursor, 8uz);
}

void test_words()
{
    auto a = area("Headwind  out,\ntailwind back");
    key(a, KEY_LEFT, 1, Word);
    CHECK_EQ(a.cursor, 24uz); // Before "back"
    key(a, KEY_LEFT, 2, Word);
    CHECK_EQ(a.cursor, 10uz); // Before "out,", over the line break
    key(a, KEY_RIGHT, 1, Word);
    CHECK_EQ(a.cursor, 14uz); // After "out,"
    key(a, KEY_RIGHT, 1, Word);
    CHECK_EQ(a.cursor, 23uz); // After "tailwind"
    key(a, KEY_BACKSPACE, 1, Word);
    CHECK_EQ(a.utf8(), "Headwind  out,\n back");
    CHECK_EQ(a.cursor, 15uz);
    // At the start, there's no word to delete.
    a.cursor = 0;
    key(a, KEY_BACKSPACE, 1, Word);
    CHECK_EQ(a.utf8(), "Headwind  out,\n back");
}

void test_paste()
{
    auto a = area("");
    a.handle_key(Paste, KEY_V, [] { return std::string { "one\r\ntwo\rthree\tfour\nfive" }; });
    CHECK_EQ(a.utf8(), "one\ntwo\nthree four\nfive");
    CHECK_EQ(a.lines.size(), 4uz);
    // Without a clipboard, nothing.
    a.handle_key(Paste, KEY_V, { });
    CHECK_EQ(a.utf8(), "one\ntwo\nthree four\nfive");
}

// A click goes to the nearest place on the line, at most to its end, and
// one below the last line to the last line.
void test_position_at()
{
    auto a = area("the quick brown fox", 10);
    CHECK_EQ(a.position_at(0, 0), 0uz);
    CHECK_EQ(a.position_at(0, 34), 3uz);
    CHECK_EQ(a.position_at(0, 36), 4uz);
    CHECK_EQ(a.position_at(0, 500), 9uz); // Not past "quick", onto the next line
    CHECK_EQ(a.position_at(1, 500), 19uz);
    CHECK_EQ(a.position_at(7, 16), 12uz); // Past the last line: the last line
}

void test_scrolling()
{
    auto a = area("1\n2\n3\n4\n5\n6\n7\n8");
    a.visible_lines = 3;
    a.scroll_to_cursor();
    CHECK_EQ(a.top_line, 5uz); // The cursor's at the end
    key(a, KEY_UP, 4);
    CHECK_EQ(a.top_line, 3uz);
    a.scroll(-10);
    CHECK_EQ(a.top_line, 0uz);
    a.scroll(10);
    CHECK_EQ(a.top_line, 5uz);
    // Text getting shorter keeps the top within the lines.
    a.set("1");
    CHECK_EQ(a.top_line, 0uz);
}

// Added by the Reviewer: a click puts the cursor somewhere else, as the
// box does it, and Up and Down after it aim from there, not from where an
// earlier Up or Down started.
void test_click_forgets_the_goal()
{
    auto a = area("abcdefgh\nab\nabcdefgh");
    a.cursor = 6;
    key(a, KEY_DOWN); // Aims for x 60
    // A click at x 10 on the first line.
    a.click(0, 10);
    CHECK_EQ(a.cursor, 1uz);
    key(a, KEY_DOWN);
    CHECK_EQ(a.cursor, 10uz); // After the "a" of "ab", below the click
}

// Added by the Reviewer: a rune outside the BMP, such as an emoji, is one
// character: one step of the cursor, one Backspace, and UTF-8 of four
// bytes.
void test_runes_outside_the_bmp()
{
    auto a = area("\xF0\x9F\x9A\xB4 ride");
    CHECK_EQ(a.text.length(), 6uz);
    CHECK_EQ(a.cursor, 6uz);
    a.cursor = 1;
    key(a, KEY_BACKSPACE);
    CHECK_EQ(a.utf8(), " ride");
    CHECK(a.insert(static_cast<rune>(0x1F3CA)));
    CHECK_EQ(a.utf8(), "\xF0\x9F\x8F\x8A ride");
    CHECK_EQ(a.cursor, 1uz);
    // Halves of a UTF-16 pair aren't characters.
    CHECK(!a.insert(static_cast<rune>(0xD83D)));
    a.paste("\xF0\x9F\x9A\xB4");
    CHECK_EQ(a.utf8(), "\xF0\x9F\x8F\x8A\xF0\x9F\x9A\xB4 ride");
    CHECK_EQ(a.cursor, 2uz);
    // Text that isn't UTF-8 isn't pasted at all.
    a.paste("\xFF");
    CHECK_EQ(a.text.length(), 7uz);
}

// Added by the Reviewer: a line that wraps after several spaces ends, for
// End and for a click past it, before the first of them.
void test_end_before_several_spaces()
{
    auto a = area("aaaa    bbbb", 6);
    CHECK(a.lines == (std::vector<Line> { { 0, 8 }, { 8, 12 } }));
    a.cursor = 1;
    key(a, KEY_END);
    CHECK_EQ(a.cursor, 4uz);
    CHECK_EQ(a.position_at(0, 500), 4uz);
    CHECK_EQ(a.line_of(7), 0uz);
    CHECK_EQ(a.line_of(8), 1uz);
}

// Added by the Reviewer: Up and Down over a word broken in pieces, and
// the line the break is on.
void test_up_and_down_over_a_broken_word()
{
    auto a = area("abcdefghijklmnop", 10);
    CHECK(a.lines == (std::vector<Line> { { 0, 10 }, { 10, 16 } }));
    CHECK_EQ(a.line_of(10), 1uz);
    a.cursor = 3;
    key(a, KEY_DOWN);
    CHECK_EQ(a.cursor, 13uz);
    key(a, KEY_UP);
    CHECK_EQ(a.cursor, 3uz);
    // From the end of the second line back up at the same x.
    key(a, KEY_DOWN);
    key(a, KEY_END);
    CHECK_EQ(a.cursor, 16uz);
    key(a, KEY_UP);
    CHECK_EQ(a.cursor, 6uz);
}

// Added by the Reviewer: typing at the end of a full line takes the word
// to the next line, and the cursor and the view with it.
void test_typing_wraps_and_scrolls()
{
    auto a = area("the quick", 10);
    a.visible_lines = 1;
    type(a, L" brown");
    CHECK(a.lines == (std::vector<Line> { { 0, 10 }, { 10, 15 } }));
    CHECK_EQ(a.line_of(a.cursor), 1uz);
    CHECK_EQ(a.top_line, 1uz);
    // Backspacing it back onto the first line scrolls back up.
    key(a, KEY_BACKSPACE, 6);
    CHECK_EQ(a.utf8(), "the quick");
    CHECK_EQ(a.top_line, 0uz);
}

// Added by the Reviewer: the wheel can't scroll a text shorter than the
// box, and a box that grows shows more lines rather than empty ones.
void test_scrolling_a_short_text()
{
    auto a = area("1\n2");
    a.visible_lines = 10;
    a.scroll(3);
    CHECK_EQ(a.top_line, 0uz);
    a.scroll(-3);
    CHECK_EQ(a.top_line, 0uz);
    auto b = area("1\n2\n3\n4\n5");
    b.visible_lines = 2;
    b.scroll_to_cursor();
    CHECK_EQ(b.top_line, 3uz);
    b.visible_lines = 4;
    b.scroll_to_cursor();
    CHECK_EQ(b.top_line, 1uz);
}

// Added by the Reviewer: deleting a word takes the spaces and line breaks
// before the cursor with it, and the word before those.
void test_delete_word_over_a_line_break()
{
    auto a = area("one two\n  ");
    key(a, KEY_BACKSPACE, 1, Word);
    CHECK_EQ(a.utf8(), "one ");
    CHECK_EQ(a.cursor, 4uz);
    CHECK(a.lines == (std::vector<Line> { { 0, 4 } }));
    // In the middle of a word: back to its start, leaving the rest.
    auto b = area("headwind");
    b.cursor = 4;
    key(b, KEY_BACKSPACE, 1, Word);
    CHECK_EQ(b.utf8(), "wind");
    CHECK_EQ(b.cursor, 0uz);
}

}

// End on a line that breaks a word goes to the break, where typing goes
// on with the word, and Left and Right step over the spaces a line wraps
// after, rather than into them, past the line's width.
void test_ends_of_wrapped_lines()
{
    auto a = area("abcdefghijklmnop", 10);
    a.cursor = 3;
    key(a, KEY_END);
    CHECK_EQ(a.cursor, 10uz);
    type(a, L"X");
    CHECK_EQ(a.utf8(), "abcdefghijXklmnop");
    CHECK_EQ(a.position_at(0, 500), 10uz);

    auto w = area("aaaa    bbbb", 6);
    w.cursor = 4;
    key(w, KEY_RIGHT);
    CHECK_EQ(w.cursor, 8uz);
    key(w, KEY_LEFT);
    CHECK_EQ(w.cursor, 4uz);
    key(w, KEY_LEFT);
    CHECK_EQ(w.cursor, 3uz);
}

// A text from elsewhere, such as Strava, has its \r\n and \r line breaks
// as line breaks, not stray characters.
void test_set_with_carriage_returns()
{
    auto const a = area("one\r\ntwo\rthree");
    CHECK_EQ(a.utf8(), "one\ntwo\nthree");
    CHECK_EQ(a.lines.size(), 3uz);
    CHECK_EQ(a.cursor, 13uz);
    // Text that isn't UTF-8 is none.
    auto b = area("one\ntwo");
    b.set("\xff\xfe");
    CHECK(b.text.empty());
    CHECK(b.lines == (std::vector<Line> { { 0, 0 } }));
}

// A long paste is wrapped once, not again after each character.
void test_paste_wraps_once()
{
    size_t   measured = 0;
    TextArea a;
    a.set_width(400, [&measured](rune_view piece) {
        ++measured;
        return 10.0f * static_cast<float>(piece.length());
    });
    std::string words;
    for (auto ix = 0; ix < 200; ++ix) {
        words += "word ";
    }
    measured = 0;
    a.handle_key(Paste, KEY_V, [&words] { return words; });
    CHECK_EQ(a.text.length(), 1000uz);
    CHECK(measured < 2000);
}

// An emoji is one character, also a flag or a family of several
// codepoints (specs/42-emoji.md): moving, deleting, clicking, and a line
// broken in a word made of emoji.
void test_emoji_are_one_character()
{
    set_emoji_set(EmojiSet::from_names({
        "1f44d.png",
        "1f44d-1f3fd.png",
        "1f1e8-1f1e6.png",
        "1f468.png",
        "1f469.png",
        "1f467.png",
        "1f468-200d-1f469-200d-1f467.png",
    }));
    // a🇨🇦b👨‍👩‍👧
    auto a = area("a\U0001F1E8\U0001F1E6b\U0001F468\u200D\U0001F469\u200D\U0001F467");
    key(a, KEY_LEFT);
    CHECK_EQ(a.cursor, 4uz);
    key(a, KEY_LEFT, 2);
    CHECK_EQ(a.cursor, 1uz);
    key(a, KEY_RIGHT);
    CHECK_EQ(a.cursor, 3uz);
    key(a, KEY_BACKSPACE);
    CHECK_EQ(a.utf8(), "ab\U0001F468\u200D\U0001F469\u200D\U0001F467");
    key(a, KEY_RIGHT);
    key(a, KEY_DELETE);
    CHECK_EQ(a.utf8(), "ab");
    // A click lands before or after the flag, not in it.
    a.set("a\U0001F1E8\U0001F1E6b");
    CHECK_EQ(a.position_at(0, 20), 1uz);
    CHECK_EQ(a.position_at(0, 26), 3uz);
    // Three flags on lines two characters wide: one flag a line.
    auto narrow = area("\U0001F1E8\U0001F1E6\U0001F1E8\U0001F1E6\U0001F1E8\U0001F1E6", 3);
    CHECK(narrow.lines == (std::vector<Line> { { 0, 2 }, { 2, 4 }, { 4, 6 } }));
    set_emoji_set(EmojiSet { });
}

int main()
{
    Test::ScopedTestApp app;
    test_emoji_are_one_character();
    test_typing();
    test_deleting_across_a_line_break();
    test_wrapping();
    test_line_of_and_end();
    test_left_and_right();
    test_up_and_down();
    test_typing_forgets_the_goal();
    test_text_start_and_end();
    test_words();
    test_paste();
    test_paste_wraps_once();
    test_ends_of_wrapped_lines();
    test_set_with_carriage_returns();
    test_position_at();
    test_scrolling();
    test_click_forgets_the_goal();
    test_runes_outside_the_bmp();
    test_end_before_several_spaces();
    test_up_and_down_over_a_broken_word();
    test_typing_wraps_and_scrolls();
    test_scrolling_a_short_text();
    test_delete_word_over_a_line_break();
    return Test::test_result();
}
