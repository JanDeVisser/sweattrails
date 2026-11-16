/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests converting between UTF-8 and UTF-32, also from two threads at
 * once, as the UI draws text while a job reads device names.
 */

#include <atomic>
#include <string>
#include <thread>

#include <Utf8.h>

#include <TestApp.h>

using namespace ST;
using namespace ST::Test;

namespace {

void test_round_trip()
{
    CHECK(to_utf32("0:10").value_or(U"") == U"0:10");
    CHECK(to_utf32("Café ☕").value_or(U"") == U"Café ☕");
    CHECK_EQ(to_utf8(U"Café ☕").value_or(""), "Café ☕");
}

// One code point of each length, at the edges of the lengths.
void test_lengths()
{
    std::u32string const runes { U'\x7F', U'\x80', U'\u07FF', U'\u0800', U'\uFFFF', U'\U00010000', U'\U0010FFFF' };
    std::string const    utf8 = "\x7F\xC2\x80\xDF\xBF\xE0\xA0\x80\xEF\xBF\xBF\xF0\x90\x80\x80\xF4\x8F\xBF\xBF";
    CHECK(to_utf32(utf8).value_or(U"") == runes);
    CHECK_EQ(to_utf8(runes).value_or(""), utf8);
    // An emoji is one rune, as it wasn't with a 16-bit wchar_t.
    CHECK_EQ(to_utf32("\xF0\x9F\x91\x8D").value_or(U"").length(), 1u);
    CHECK(to_utf32("").value_or(U"x").empty());
}

void test_invalid_utf8()
{
    for (std::string_view const bad : {
             "\x80",             // A continuation byte without a lead
             "a\xC3",            // Cut off
             "\xE2\x98",         // Cut off
             "\xC3(",            // Not a continuation byte
             "\xC0\xAF",         // Overlong '/'
             "\xE0\x80\xAF",     // Overlong '/'
             "\xED\xA0\x80",     // A surrogate
             "\xF4\x90\x80\x80", // Past U+10FFFF
             "\xF8\x88\x80\x80\x80",
             "\xFF",
         }) {
        CHECK(!to_utf32(bad).has_value());
    }
}

void test_invalid_utf32()
{
    CHECK(!to_utf8(std::u32string { U'a', char32_t { 0xD800 } }).has_value());
    CHECK(!to_utf8(std::u32string { char32_t { 0x110000 } }).has_value());
}

void test_is_code_point()
{
    for (auto const ch : std::u32string { 0x0, 0x41, 0xD7FF, 0xE000, 0xFFFF, 0x1F44D, 0x10FFFF }) {
        CHECK(is_code_point(ch));
    }
    for (auto const ch : std::u32string { 0xD800, 0xDBFF, 0xDC00, 0xDFFF, 0x110000, 0xFFFFFFFF }) {
        CHECK(!is_code_point(ch));
    }
}

void test_classes()
{
    for (auto const ch : std::u32string_view { U" \t\n\r\u2003\u3000" }) {
        CHECK(is_space(ch));
    }
    // The no-break spaces don't break a line.
    for (auto const ch : std::u32string_view { U"a\u00A0\u2007\u202F\U0001F44D" }) {
        CHECK(!is_space(ch));
    }
    for (auto const ch : std::u32string_view { U"\x01\x1F\x7F\u0085\u009F\u2028" }) {
        CHECK(is_control(ch));
    }
    for (auto const ch : std::u32string_view { U" a\u00A0\u00E9\U0001F44D" }) {
        CHECK(!is_control(ch));
    }
}

// Each thread converts its own text, over and over, and counts what
// comes out wrong.
void test_two_threads()
{
    std::atomic<int> wrong { 0 };
    auto const       convert = [&wrong](std::string_view text, std::u32string_view expected) {
        for (auto ix = 0; ix < 100'000; ++ix) {
            if (to_utf32(text).value_or(U"") != expected || to_utf8(expected).value_or("") != text) {
                ++wrong;
            }
        }
    };
    std::thread job { convert, "Edge 1030 Plus", U"Edge 1030 Plus" };
    convert("Alt 12–340 m", U"Alt 12–340 m");
    job.join();
    CHECK_EQ(wrong.load(), 0);
}

}

int main()
{
    test_round_trip();
    test_lengths();
    test_invalid_utf8();
    test_invalid_utf32();
    test_is_code_point();
    test_classes();
    test_two_threads();
    return test_result();
}
