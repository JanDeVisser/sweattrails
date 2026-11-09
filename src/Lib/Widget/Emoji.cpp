/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <charconv>
#include <ranges>

#include <Emoji.h>

namespace ST {

namespace {

std::optional<EmojiSet> the_set { };

// "1f44d-1f3fd.png" as its characters; none for a name that isn't one.
std::optional<rune_string> sequence_of(std::string_view name)
{
    if (!name.ends_with(".png")) {
        return { };
    }
    name.remove_suffix(4);
    rune_string ret;
    for (auto const part : std::views::split(name, '-')) {
        std::string_view const hex { part.begin(), part.end() };
        u32                    codepoint = 0;
        auto const [end, error] = std::from_chars(hex.data(), hex.data() + hex.size(), codepoint, 16);
        if (hex.empty() || error != std::errc { } || end != hex.data() + hex.size()) {
            return { };
        }
        if (codepoint != static_cast<u32>(VariationSelector16)) {
            ret.push_back(static_cast<rune>(codepoint));
        }
    }
    if (ret.empty()) {
        return { };
    }
    return ret;
}

// The emoji at `start`, if there is one: where it ends, with the U+FE0F
// in and after it, and its image.
struct Match {
    size_t      end;
    std::string image;
};

std::optional<Match> match_at(rune_view text, size_t start, EmojiSet const &set, HasGlyph const &has_glyph)
{
    if (!set.starts.contains(text[start])) {
        return { };
    }
    std::optional<Match> best { };
    size_t               best_length = 0; // In characters, without U+FE0F
    rune_string          key;
    for (auto ix = start; ix < text.size() && key.size() < set.longest; ++ix) {
        if (text[ix] == VariationSelector16) {
            continue;
        }
        key.push_back(text[ix]);
        if (auto const it = set.sequences.find(key); it != set.sequences.end()) {
            best = Match { .end = ix + 1, .image = it->second };
            best_length = key.size();
        }
    }
    if (!best) {
        return { };
    }
    // A single character the font has is text, unless U+FE0F asks for
    // its picture.
    auto const asked = best->end < text.size() && text[best->end] == VariationSelector16;
    if (best_length == 1 && !asked && has_glyph && has_glyph(text[start])) {
        return { };
    }
    while (best->end < text.size() && text[best->end] == VariationSelector16) {
        ++best->end;
    }
    return best;
}

}

EmojiSet EmojiSet::from_names(std::vector<std::string> const &names)
{
    EmojiSet ret;
    for (auto const &name : names) {
        auto sequence = sequence_of(name);
        if (!sequence) {
            continue;
        }
        ret.starts.insert(sequence->front());
        ret.longest = std::max(ret.longest, sequence->size());
        ret.sequences.emplace(std::move(*sequence), name);
    }
    return ret;
}

EmojiSet const *emoji_set()
{
    return the_set ? &*the_set : nullptr;
}

void set_emoji_set(EmojiSet set)
{
    the_set = std::move(set);
}

std::vector<TextRun> split_emoji(rune_view text, EmojiSet const &set, HasGlyph const &has_glyph)
{
    std::vector<TextRun> ret;
    auto const           add = [&ret](RunKind kind, size_t start, size_t end, std::string image = { }) {
        // Text after text is one run.
        if (kind == RunKind::Text && !ret.empty() && ret.back().kind == RunKind::Text && ret.back().end == start) {
            ret.back().end = end;
            return;
        }
        ret.push_back(TextRun { .kind = kind, .start = start, .end = end, .image = std::move(image) });
    };
    for (size_t ix = 0; ix < text.size();) {
        if (auto match = match_at(text, ix, set, has_glyph); match) {
            add(RunKind::Emoji, ix, match->end, std::move(match->image));
            ix = match->end;
        } else if (text[ix] == VariationSelector16 || text[ix] == ZeroWidthJoiner) {
            add(RunKind::Hidden, ix, ix + 1);
            ++ix;
        } else {
            add(RunKind::Text, ix, ix + 1);
            ++ix;
        }
    }
    return ret;
}

size_t next_character(rune_view text, size_t position, EmojiSet const *set, HasGlyph const &has_glyph)
{
    if (position >= text.size()) {
        return text.size();
    }
    if (set != nullptr) {
        for (auto const &run : split_emoji(text, *set, has_glyph)) {
            if (run.kind == RunKind::Emoji && run.start <= position && position < run.end) {
                return run.end;
            }
        }
    }
    return position + 1;
}

size_t previous_character(rune_view text, size_t position, EmojiSet const *set, HasGlyph const &has_glyph)
{
    if (position == 0) {
        return 0;
    }
    position = std::min(position, text.size());
    if (set != nullptr) {
        for (auto const &run : split_emoji(text, *set, has_glyph)) {
            if (run.kind == RunKind::Emoji && run.start < position && position <= run.end) {
                return run.start;
            }
        }
    }
    return position - 1;
}

float runs_width(rune_view text, std::vector<TextRun> const &runs, std::function<float(rune_view)> const &measure, float emoji_width, float spacing)
{
    auto width = 0.0f;
    auto drawn = 0;
    for (auto const &run : runs) {
        if (run.kind == RunKind::Hidden) {
            continue;
        }
        width += (run.kind == RunKind::Emoji) ? emoji_width : measure(text.substr(run.start, run.end - run.start));
        ++drawn;
    }
    return width + static_cast<float>(std::max(0, drawn - 1)) * spacing;
}

}
