/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests finding emoji in text: sequences as one, the longest match,
 * U+FE0F, characters the font has, and where the cursor steps, with a
 * few of Twemoji's file names and a font with ASCII and ©.
 */

#include <string>
#include <vector>

#include <Emoji.h>

#include <TestApp.h>

using namespace ST;

namespace {

EmojiSet const &set()
{
    static EmojiSet const ret = EmojiSet::from_names({
        "1f44d.png",                       // 👍
        "1f3fd.png",                       // 🏽
        "1f44d-1f3fd.png",                 // 👍🏽
        "1f1e8-1f1e6.png",                 // 🇨🇦
        "31-20e3.png",                     // 1️⃣
        "1f468.png",                       // 👨
        "1f469.png",                       // 👩
        "1f467.png",                       // 👧
        "1f468-200d-1f469-200d-1f467.png", // 👨‍👩‍👧
        "1f3c3-200d-2640-fe0f.png",        // 🏃‍♀️
        "2764.png",                        // ❤
        "a9.png",                          // ©
        "LICENSE",
        "not-hex.png",
    });
    return ret;
}

bool has_glyph(rune ch)
{
    return ch < 0x7F || ch == 0xA9;
}

std::vector<TextRun> split(rune_view text)
{
    return split_emoji(text, set(), has_glyph);
}

TextRun text(size_t start, size_t end)
{
    return TextRun { .kind = RunKind::Text, .start = start, .end = end };
}

TextRun emoji(size_t start, size_t end, std::string image)
{
    return TextRun { .kind = RunKind::Emoji, .start = start, .end = end, .image = std::move(image) };
}

TextRun hidden(size_t at)
{
    return TextRun { .kind = RunKind::Hidden, .start = at, .end = at + 1 };
}

void test_names()
{
    CHECK_EQ(set().sequences.size(), 12uz);
    CHECK_EQ(set().longest, 5uz);
    // The U+FE0F in a name is left out of its sequence.
    CHECK(set().sequences.contains(rune_string { 0x1F3C3, 0x200D, 0x2640 }));
    CHECK(set().starts.contains(0x31));
    CHECK(!set().starts.contains(0x1F3FD + 1));
}

void test_text_without_emoji()
{
    CHECK(split(L"Morning ride") == (std::vector { text(0, 12) }));
    CHECK(split(L"").empty());
    // A digit is text, though it starts a keycap.
    CHECK(split(L"10 km") == (std::vector { text(0, 5) }));
}

void test_sequences_are_one()
{
    // Ride 👍🏽 🇨🇦 1️⃣ 👨‍👩‍👧
    rune_string const t = L"Ride \U0001F44D\U0001F3FD \U0001F1E8\U0001F1E6 1️⃣ \U0001F468‍\U0001F469‍\U0001F467";
    CHECK(split(t) == (std::vector {
              text(0, 5),
              emoji(5, 7, "1f44d-1f3fd.png"),
              text(7, 8),
              emoji(8, 10, "1f1e8-1f1e6.png"),
              text(10, 11),
              emoji(11, 14, "31-20e3.png"),
              text(14, 15),
              emoji(15, 20, "1f468-200d-1f469-200d-1f467.png"),
          }));
}

// U+FE0F inside a sequence, where the name has it, and after it, where
// the name hasn't, is taken with the emoji.
void test_variation_selectors()
{
    CHECK(split(L"\U0001F3C3‍♀️!") == (std::vector { emoji(0, 4, "1f3c3-200d-2640-fe0f.png"), text(4, 5) }));
    CHECK(split(L"❤️") == (std::vector { emoji(0, 2, "2764.png") }));
    CHECK(split(L"❤") == (std::vector { emoji(0, 1, "2764.png") }));
}

// © is text, as the font has it, but ©️ asks for the picture.
void test_characters_the_font_has()
{
    CHECK(split(L"© 2026") == (std::vector { text(0, 6) }));
    CHECK(split(L"©️") == (std::vector { emoji(0, 2, "a9.png") }));
}

// A family there's no image for is its people, and a codepoint without
// an image stays text.
void test_parts_without_an_image()
{
    // 👨‍👧, which the set hasn't
    CHECK(split(L"\U0001F468‍\U0001F467") == (std::vector { emoji(0, 1, "1f468.png"), hidden(1), emoji(2, 3, "1f467.png") }));
    CHECK(split(L"\U0001F680") == (std::vector { text(0, 1) }));
    // A stray U+FE0F draws as nothing.
    CHECK(split(L"a️b") == (std::vector { text(0, 1), hidden(1), text(2, 3) }));
}

void test_cursor_steps()
{
    // a👍🏽b👨‍👩‍👧
    rune_string const t = L"a\U0001F44D\U0001F3FDb\U0001F468‍\U0001F469‍\U0001F467";
    CHECK_EQ(next_character(t, 0, &set(), has_glyph), 1uz);
    CHECK_EQ(next_character(t, 1, &set(), has_glyph), 3uz);
    CHECK_EQ(next_character(t, 4, &set(), has_glyph), 9uz);
    CHECK_EQ(next_character(t, 9, &set(), has_glyph), 9uz);
    CHECK_EQ(previous_character(t, 9, &set(), has_glyph), 4uz);
    CHECK_EQ(previous_character(t, 3, &set(), has_glyph), 1uz);
    CHECK_EQ(previous_character(t, 1, &set(), has_glyph), 0uz);
    CHECK_EQ(previous_character(t, 0, &set(), has_glyph), 0uz);
    // Without a set, a rune at a time.
    CHECK_EQ(next_character(t, 1, nullptr, has_glyph), 2uz);
    CHECK_EQ(previous_character(t, 9, nullptr, has_glyph), 8uz);
}

void test_runs_width()
{
    rune_string const t = L"ab\U0001F44D️c";
    auto const        runs = split(t);
    auto const        measure = [](rune_view piece) { return 10.0f * static_cast<float>(piece.size()); };
    // "ab", the emoji, "c": 20 + 30 + 10, and spacing between the three.
    CHECK_EQ(runs_width(t, runs, measure, 30.0f, 2.0f), 64.0f);
    CHECK_EQ(runs_width(t, { }, measure, 30.0f, 2.0f), 0.0f);
}

}

int main()
{
    test_names();
    test_text_without_emoji();
    test_sequences_are_one();
    test_variation_selectors();
    test_characters_the_font_has();
    test_parts_without_an_image();
    test_cursor_steps();
    test_runs_width();
    return Test::test_result();
}
