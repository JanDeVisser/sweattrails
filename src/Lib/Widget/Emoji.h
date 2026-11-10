/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Emoji in text, drawn as Twemoji's images (specs/42-emoji.md): which
 * sequences of characters there are images for, where they are in a
 * text, and where the cursor steps over them. No drawing, so that tests
 * reach it without a window.
 */

#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <Widget.h>

namespace ST {

// Asks for a character's text form rather than its picture: ©︎.
constexpr rune VariationSelector15 = 0xFE0E;
// Asks for an emoji's picture rather than its text form: ©️ rather than ©.
constexpr rune VariationSelector16 = 0xFE0F;
// Joins emoji into one, such as the people of a family.
constexpr rune ZeroWidthJoiner = 0x200D;
// The tags that name a subdivision in its flag: England is 🏴 and "gbeng".
constexpr rune TagFirst = 0xE0020;
constexpr rune TagLast = 0xE007F;

// The sequences there are images for, from the names of Twemoji's files,
// "1f44d-1f3fd.png": the codepoints in hex, joined with "-". Matching
// leaves out U+FE0F, which Twemoji's names have in some sequences and
// not in others, and which texts have or leave out as they like.
struct EmojiSet {
    std::map<rune_string, std::string> sequences { }; // Without U+FE0F, to the file's name
    std::set<rune>                     starts { };    // The first characters of the sequences
    size_t                             longest { 0 }; // In characters, without U+FE0F

    // From file names; ones that aren't a sequence in hex are passed over.
    [[nodiscard]] static EmojiSet from_names(std::vector<std::string> const &names);

    [[nodiscard]] bool empty() const { return sequences.empty(); }
};

// The emoji the app draws, which App::set_emoji() sets. None until then,
// as in tests that don't set any: then there are no emoji.
[[nodiscard]] EmojiSet const *emoji_set();
void                          set_emoji_set(EmojiSet set);

// Whether the font has a character: one it has is only an emoji when
// U+FE0F asks for that, as Twemoji has images of ©, ® and the digits.
using HasGlyph = std::function<bool(rune)>;

enum class RunKind {
    Text,
    Emoji,
    Hidden, // A U+FE0E, U+FE0F, U+200D or tag that isn't part of an emoji: drawn as nothing
};

// A part of a text: text[start, end).
struct TextRun {
    RunKind     kind { RunKind::Text };
    size_t      start { 0 };
    size_t      end { 0 };
    std::string image { }; // For an emoji, the file's name

    bool operator==(TextRun const &) const = default;
};

// The text in runs of text and emoji, each emoji the longest sequence
// there's an image for at its place, with the U+FE0F in and after it.
// A sequence without an image is its parts that have one. An empty text
// is no runs.
[[nodiscard]] std::vector<TextRun> split_emoji(rune_view text, EmojiSet const &set, HasGlyph const &has_glyph);

// Where the cursor goes from `position`, a character on or back: over all
// of an emoji, or else over one rune, and over what's drawn as nothing
// with it, so that each stop is a place of its own.
[[nodiscard]] size_t next_character(rune_view text, size_t position, EmojiSet const *set, HasGlyph const &has_glyph);
[[nodiscard]] size_t previous_character(rune_view text, size_t position, EmojiSet const *set, HasGlyph const &has_glyph);

// The same, with the text's runs from split_emoji(), for an editor that
// steps many times through the same text.
[[nodiscard]] size_t next_character(rune_view text, std::vector<TextRun> const &runs, size_t position);
[[nodiscard]] size_t previous_character(rune_view text, std::vector<TextRun> const &runs, size_t position);

// How wide the runs are drawn side by side, with `spacing` between each
// two characters and between the runs, as MeasureTextEx() counts it:
// text as `measure` measures it, and each emoji `emoji_width`.
[[nodiscard]] float runs_width(rune_view text, std::vector<TextRun> const &runs, std::function<float(rune_view)> const &measure, float emoji_width, float spacing);

}
