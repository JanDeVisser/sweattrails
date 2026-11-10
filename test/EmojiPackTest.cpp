/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the committed Twemoji pack, share/emoji/twemoji.zip, as the app
 * loads it with App::set_emoji(): that the emoji of the spec match its
 * real file names, with the app's own idea of what the font has, and
 * that their PNGs come out of the ZIP (specs/42-emoji.md).
 */

#include <string>
#include <vector>

#include <Zip.h>

#include <App.h>
#include <Emoji.h>

#include <TestApp.h>

using namespace ST;

namespace {

std::string const PackPath = SWEATTRAILS_SOURCE_DIR "/share/emoji/twemoji.zip";

// The one emoji `text` is, or its runs' images joined with " " if it
// isn't one.
std::string image_of(rune_view text)
{
    auto const runs = split_emoji(text, *emoji_set(), App::font_has);
    if (runs.size() == 1 && runs.front().kind == RunKind::Emoji && runs.front().end == text.size()) {
        return runs.front().image;
    }
    std::string ret;
    for (auto const &run : runs) {
        ret += std::format("{}{}:{}", ret.empty() ? "" : " ", static_cast<int>(run.kind), run.image);
    }
    return ret;
}

void test_the_pack_loads()
{
    CHECK(emoji_set() != nullptr);
    if (emoji_set() == nullptr) {
        return;
    }
    // Twemoji 17 has some 3,800 of them.
    CHECK(emoji_set()->sequences.size() > 3500);
    CHECK(!App::the()->emoji_zip.empty());
}

void test_sequences_match_the_real_names()
{
    CHECK_EQ(image_of(L"\U0001F44D\U0001F3FD"), "1f44d-1f3fd.png");
    CHECK_EQ(image_of(L"\U0001F1E8\U0001F1E6"), "1f1e8-1f1e6.png");
    // 1️⃣, with and without U+FE0F
    CHECK_EQ(image_of(L"1️⃣"), "31-20e3.png");
    CHECK_EQ(image_of(L"1⃣"), "31-20e3.png");
    CHECK_EQ(image_of(L"#️⃣"), "23-20e3.png");
    CHECK_EQ(image_of(L"\U0001F468‍\U0001F469‍\U0001F467"), "1f468-200d-1f469-200d-1f467.png");
    // 🏃‍♀️: Twemoji's name has the U+FE0F, the text may leave it out.
    CHECK_EQ(image_of(L"\U0001F3C3‍♀️"), "1f3c3-200d-2640-fe0f.png");
    CHECK_EQ(image_of(L"\U0001F3C3‍♀"), "1f3c3-200d-2640-fe0f.png");
    // 🏳️‍🌈, with U+FE0F in the middle of the name.
    CHECK_EQ(image_of(L"\U0001F3F3️‍\U0001F308"), "1f3f3-fe0f-200d-1f308.png");
    CHECK_EQ(image_of(L"\U0001F3F3‍\U0001F308"), "1f3f3-fe0f-200d-1f308.png");
    // 🏴󠁧󠁢󠁥󠁮󠁧󠁿, a flag of tag characters.
    CHECK_EQ(image_of(L"\U0001F3F4\U000E0067\U000E0062\U000E0065\U000E006E\U000E0067\U000E007F"), "1f3f4-e0067-e0062-e0065-e006e-e0067-e007f.png");
    CHECK_EQ(image_of(L"❤️"), "2764.png");
    CHECK_EQ(image_of(L"©️"), "a9.png");
    CHECK_EQ(image_of(L"‼️"), "203c.png");
}

// What the font has stays text: © and ‼ (General Punctuation), and
// digits and # though they start keycaps.
void test_text_the_font_has()
{
    for (auto const *t : { L"© 2026", L"‼", L"#1 at 12:34", L"Ride 42 km" }) {
        rune_view const text { t };
        CHECK(split_emoji(text, *emoji_set(), App::font_has) == (std::vector { TextRun { .kind = RunKind::Text, .start = 0, .end = text.size() } }));
    }
}

void test_images_come_out_of_the_zip()
{
    auto const &zip = App::the()->emoji_zip;
    for (auto const *name : { "1f44d-1f3fd.png", "1f1e8-1f1e6.png", "31-20e3.png", "1f468-200d-1f469-200d-1f467.png", "a9.png" }) {
        auto const png = unzip(zip, name);
        CHECK(png.has_value());
        if (png) {
            CHECK(png->starts_with("\x89PNG\r\n\x1a\n"));
        }
    }
}

}

int main()
{
    Test::ScopedTestApp app;
    app.app->set_emoji(PackPath);
    test_the_pack_loads();
    if (emoji_set() != nullptr) {
        test_sequences_match_the_real_names();
        test_text_the_font_has();
        test_images_come_out_of_the_zip();
    }
    return Test::test_result();
}
