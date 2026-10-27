/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The characters the fonts are loaded with. Loading a font needs a window;
 * which characters it gets doesn't.
 */

#include <FontCache.h>

#include <TestApp.h>

using namespace ST;

namespace {

void test_add_codepoints()
{
    FontCache fonts;
    fonts.set_codepoints({ 0xb0 });
    // ASCII, and what's there already: nothing to load again.
    CHECK(!fonts.add_codepoints("Morning Ride"));
    CHECK(!fonts.add_codepoints("21°"));
    CHECK(fonts.add_codepoints("Rückweg über den Paß"));
    CHECK_EQ(fonts.codepoints().size(), 3uz);
    CHECK(fonts.codepoints().contains(L'ü'));
    CHECK(fonts.codepoints().contains(L'ß'));
    CHECK(!fonts.add_codepoints("Rückweg"));
    CHECK(!fonts.add_codepoints(""));
}

}

int main()
{
    test_add_codepoints();
    return ST::Test::test_result();
}
