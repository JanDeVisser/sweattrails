/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <string>

#include <StringUtil.h>

#include <TestApp.h>

using namespace ST;

namespace {

void test_base64url()
{
    // RFC 4648's test vectors, without the padding.
    CHECK_EQ(base64url(""), "");
    CHECK_EQ(base64url("f"), "Zg");
    CHECK_EQ(base64url("fo"), "Zm8");
    CHECK_EQ(base64url("foo"), "Zm9v");
    CHECK_EQ(base64url("foob"), "Zm9vYg");
    CHECK_EQ(base64url("fooba"), "Zm9vYmE");
    CHECK_EQ(base64url("foobar"), "Zm9vYmFy");
}

void test_base64url_uses_the_url_safe_alphabet()
{
    // 0xFB 0xFF is "+/8" in standard base64.
    CHECK_EQ(base64url(std::string { "\xFB\xFF" }), "-_8");
}

// Only ASCII whitespace is stripped: "à" ends in the byte 0xA0, which is a
// space in Latin-1.
void test_strip()
{
    CHECK_EQ(strip("  Rückweg \t\n"), "Rückweg");
    CHECK_EQ(strip("Voilà"), "Voilà");
    CHECK_EQ(strip("\xA0Voilà\xA0"), "\xA0Voilà\xA0");
    CHECK_EQ(strip("   "), "");
    CHECK_EQ(strip(""), "");
    CHECK_EQ(lstrip("  a  "), "a  ");
    CHECK_EQ(rstrip("  a  "), "  a");
    CHECK_EQ(rstrip("   "), "");
    CHECK_EQ(rstrip(""), "");
}

// A character is 10 wide.
void test_wrap_words()
{
    auto const measure = [](std::string_view text) { return 10.0f * static_cast<float>(text.length()); };
    auto const wrapped = [&](std::string_view text, float width) {
        std::string ret;
        for (auto const &line : wrap_words(text, width, measure)) {
            ret += (ret.empty() ? "" : "|") + line;
        }
        return ret;
    };
    CHECK_EQ(wrapped("element 3: the font isn't there", 100), "element 3:|the font|isn't|there");
    CHECK_EQ(wrapped("element 3: the font isn't there", 1000), "element 3: the font isn't there");
    // A word that doesn't fit is a line of its own.
    CHECK_EQ(wrapped("a cycling-basic.overlay.json b", 50), "a|cycling-basic.overlay.json|b");
    CHECK_EQ(wrapped("  two  spaces ", 1000), "two spaces");
    CHECK_EQ(wrapped("", 100), "");
    // A description from Strava, with paragraphs.
    CHECK_EQ(wrapped("Windy.\r\n\nWith Anna and Bo", 100), "Windy.|With Anna|and Bo");
}

// A character is 10 wide, and so is "…".
void test_clamp_lines()
{
    auto const measure = [](std::string_view text) {
        auto const ellipses = static_cast<float>(text.ends_with("…") ? 1 : 0);
        return 10.0f * (static_cast<float>(text.length()) - 2 * ellipses);
    };
    auto const clamped = [&](std::vector<std::string> lines, size_t max, float width) {
        std::string ret;
        for (auto const &line : clamp_lines(std::move(lines), max, width, measure)) {
            ret += (ret.empty() ? "" : "|") + line;
        }
        return ret;
    };
    CHECK_EQ(clamped({ "one", "two" }, 3, 100), "one|two");
    CHECK_EQ(clamped({ "one", "two", "three" }, 3, 100), "one|two|three");
    CHECK_EQ(clamped({ "one", "two", "three", "four" }, 3, 100), "one|two|three…");
    // Cut short to fit with the "…", and not after a space.
    CHECK_EQ(clamped({ "one", "two", "with Anna", "four" }, 3, 60), "one|two|with…");
    CHECK_EQ(clamped({ "one", "two" }, 0, 100), "");
}
}

int main()
{
    test_base64url();
    test_base64url_uses_the_url_safe_alphabet();
    test_strip();
    test_wrap_words();
    test_clamp_lines();
    return ST::Test::test_result();
}
