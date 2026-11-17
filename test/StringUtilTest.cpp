/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
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

// A separator between each group of three digits, from the right.
void test_integer_grouping()
{
    CHECK_EQ(integer_to_string(0, 10, ','), "0");
    CHECK_EQ(integer_to_string(123, 10, ','), "123");
    CHECK_EQ(integer_to_string(1234, 10, ','), "1,234");
    CHECK_EQ(integer_to_string(123456, 10, ','), "123,456");
    CHECK_EQ(integer_to_string(1234567, 10, ','), "1,234,567");
    CHECK_EQ(integer_to_string(-1234, 10, ','), "-1,234");
    CHECK_EQ(integer_to_string(1234), "1234");
}

// Parses `text` as std::from_chars would, and says how much it took.
template<typename Float>
std::pair<std::errc, size_t> parse(std::string_view text, Float &value)
{
    auto const [end, ec] = float_from_chars(text.data(), text.data() + text.size(), value);
    return { ec, end - text.data() };
}

void test_float_from_chars()
{
    double d { 0 };
    CHECK(parse("3.25", d) == std::pair(std::errc { }, size_t { 4 }));
    CHECK_EQ(d, 3.25);
    CHECK(parse("-0.5", d) == std::pair(std::errc { }, size_t { 4 }));
    CHECK_EQ(d, -0.5);
    CHECK(parse("1e3", d) == std::pair(std::errc { }, size_t { 3 }));
    CHECK_EQ(d, 1000.0);
    CHECK(parse("0e5", d) == std::pair(std::errc { }, size_t { 3 }));
    CHECK_EQ(d, 0.0);
    CHECK(parse(".5", d) == std::pair(std::errc { }, size_t { 2 }));
    CHECK_EQ(d, 0.5);

    // It stops where the number does, as Laps' "10km" needs.
    CHECK(parse("10km", d) == std::pair(std::errc { }, size_t { 2 }));
    CHECK_EQ(d, 10.0);
    CHECK(parse("1e", d) == std::pair(std::errc { }, size_t { 1 }));
    CHECK_EQ(d, 1.0);

    // Hexadecimal is only its 0, as with from_chars.
    CHECK(parse("0x10", d) == std::pair(std::errc { }, size_t { 1 }));
    CHECK_EQ(d, 0.0);
    CHECK(parse("-0x10", d) == std::pair(std::errc { }, size_t { 2 }));

    CHECK(parse("inf", d) == std::pair(std::errc { }, size_t { 3 }));
    CHECK(std::isinf(d));
}

void test_float_from_chars_refuses()
{
    // What std::from_chars refuses, which strtod() takes. A refused number
    // leaves the value alone.
    double d { 42 };
    CHECK(parse("", d).first == std::errc::invalid_argument);
    CHECK(parse("-", d).first == std::errc::invalid_argument);
    CHECK(parse(" 1", d).first == std::errc::invalid_argument);
    CHECK(parse("+1", d).first == std::errc::invalid_argument);
    CHECK(parse("km", d).first == std::errc::invalid_argument);
    CHECK_EQ(d, 42.0);

    CHECK(parse("1e999", d) == std::pair(std::errc::result_out_of_range, size_t { 5 }));
    CHECK_EQ(d, 42.0);
}

void test_float_from_chars_float()
{
    float f { 0 };
    CHECK(parse("2.5", f) == std::pair(std::errc { }, size_t { 3 }));
    CHECK_EQ(f, 2.5f);
    // Fits a double, not a float.
    CHECK(parse("1e50", f).first == std::errc::result_out_of_range);
    CHECK_EQ(f, 2.5f);
}

int main()
{
    test_base64url();
    test_base64url_uses_the_url_safe_alphabet();
    test_strip();
    test_wrap_words();
    test_clamp_lines();
    test_integer_grouping();
    test_float_from_chars();
    test_float_from_chars_refuses();
    test_float_from_chars_float();
    return ST::Test::test_result();
}
