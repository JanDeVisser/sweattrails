/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
#include <format>
#include <string>
#include <vector>

#include <Dashes.h>

#include <TestApp.h>

using namespace ST;

namespace {

// "0,0-4,0 6,0-10,0", rounded to tenths.
std::string show(std::vector<Dash> const &dashes)
{
    std::string ret;
    auto const  point = [](Vector2 p) { return std::format("{},{}", std::round(p.x * 10) / 10, std::round(p.y * 10) / 10); };
    for (auto const &d : dashes) {
        if (!ret.empty()) {
            ret += ' ';
        }
        ret += point(d.from) + '-' + point(d.to);
    }
    return ret;
}

std::string dashed(std::vector<Vector2> const &points, float dash = 4, float gap = 2)
{
    return show(dashes(points, dash, gap));
}

void test_straight_line()
{
    CHECK_EQ(dashed({ { 0, 0 }, { 16, 0 } }), "0,0-4,0 6,0-10,0 12,0-16,0");
}

// The last dash stops where the line does.
void test_cut_short_at_the_end()
{
    CHECK_EQ(dashed({ { 0, 0 }, { 0, 8 } }), "0,0-0,4 0,6-0,8");
    CHECK_EQ(dashed({ { 0, 0 }, { 5, 0 } }), "0,0-4,0");
}

// Segments a pixel long, as a paused track's are on the map: the pattern
// goes on across them, so the line isn't solid.
void test_pattern_across_short_segments()
{
    std::vector<Vector2> points;
    for (auto x = 0; x <= 12; ++x) {
        points.push_back({ static_cast<float>(x), 0 });
    }
    CHECK_EQ(dashed(points), "0,0-1,0 1,0-2,0 2,0-3,0 3,0-4,0 6,0-7,0 7,0-8,0 8,0-9,0 9,0-10,0");
}

void test_dash_cut_at_a_corner()
{
    CHECK_EQ(dashed({ { 0, 0 }, { 3, 0 }, { 3, 6 } }), "0,0-3,0 3,0-3,1 3,3-3,6");
}

void test_nothing_to_dash()
{
    CHECK_EQ(dashed({ }), "");
    CHECK_EQ(dashed({ { 1, 1 } }), "");
    CHECK_EQ(dashed({ { 1, 1 }, { 1, 1 } }), "");
    CHECK_EQ(dashed({ { 0, 0 }, { 8, 0 } }, 0, 2), "");
}

// A point twice over, as the map has where the track stood still,
// doesn't break the pattern.
void test_zero_length_segment()
{
    CHECK_EQ(dashed({ { 0, 0 }, { 2, 0 }, { 2, 0 }, { 10, 0 } }), "0,0-2,0 2,0-4,0 6,0-10,0");
}

}

int main()
{
    test_straight_line();
    test_cut_short_at_the_end();
    test_pattern_across_short_segments();
    test_dash_cut_at_a_corner();
    test_nothing_to_dash();
    test_zero_length_segment();
    return ST::Test::test_result();
}
