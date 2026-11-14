/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests converting between UTF-8 and wide strings, also from two threads
 * at once, as the UI draws text while a job reads device names.
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
    CHECK(to_wstring("0:10").value_or(L"") == L"0:10");
    CHECK(to_wstring("Café ☕").value_or(L"") == L"Café ☕");
    CHECK_EQ(to_utf8(L"Café ☕").value_or(""), "Café ☕");
}

// Each thread converts its own text, over and over, and counts what
// comes out wrong.
void test_two_threads()
{
    std::atomic<int> wrong { 0 };
    auto const       convert = [&wrong](std::string_view text, std::wstring_view expected) {
        for (auto ix = 0; ix < 100'000; ++ix) {
            if (to_wstring(text).value_or(L"") != expected || to_utf8(expected).value_or("") != text) {
                ++wrong;
            }
        }
    };
    std::thread job { convert, "Edge 1030 Plus", L"Edge 1030 Plus" };
    convert("Alt 12–340 m", L"Alt 12–340 m");
    job.join();
    CHECK_EQ(wrong.load(), 0);
}

}

int main()
{
    test_round_trip();
    test_two_threads();
    return test_result();
}
