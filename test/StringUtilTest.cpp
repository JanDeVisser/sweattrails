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

}

int main()
{
    test_base64url();
    test_base64url_uses_the_url_safe_alphabet();
    test_strip();
    return ST::Test::test_result();
}
