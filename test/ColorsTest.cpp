/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <format>
#include <string>

#include <Colors.h>

#include <TestApp.h>

using namespace ST;

namespace {

std::string show(std::optional<Color> const &color)
{
    if (!color) {
        return "none";
    }
    return std::format("{} {} {} {}", color->r, color->g, color->b, color->a);
}

void test_colors()
{
    CHECK_EQ(show(parse_color("#ffffff")), "255 255 255 255");
    CHECK_EQ(show(parse_color("#00000080")), "0 0 0 128");
    CHECK_EQ(show(parse_color("#F94144")), "249 65 68 255");
    CHECK_EQ(show(parse_color("#f9414430")), "249 65 68 48");
}

void test_not_colors()
{
    CHECK_EQ(show(parse_color("")), "none");
    CHECK_EQ(show(parse_color("ffffff")), "none");
    CHECK_EQ(show(parse_color("#fff")), "none");
    CHECK_EQ(show(parse_color("#fffffff")), "none");
    CHECK_EQ(show(parse_color("#ggffff")), "none");
    CHECK_EQ(show(parse_color("#ff ff ")), "none");
    CHECK_EQ(show(parse_color("#+fffff")), "none");
    CHECK_EQ(show(parse_color("white")), "none");
}

}

int main()
{
    test_colors();
    test_not_colors();
    return ST::Test::test_result();
}
