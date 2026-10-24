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

}

int main()
{
    test_base64url();
    test_base64url_uses_the_url_safe_alphabet();
    return ST::Test::test_result();
}
