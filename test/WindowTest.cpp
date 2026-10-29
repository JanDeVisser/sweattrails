/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <format>
#include <string>

#include <Window.h>

#include <TestApp.h>

using namespace ST;

namespace {

std::string show(Rectangle const &r)
{
    return std::format("{},{} {}x{}", r.x, r.y, r.width, r.height);
}

void test_start_monitor()
{
    CHECK_EQ(start_monitor(0, 1), 0);
    CHECK_EQ(start_monitor(0, 2), 0);
    CHECK_EQ(start_monitor(1, 2), 1);
    // Unplugged since the last run.
    CHECK_EQ(start_monitor(2, 2), 0);
    CHECK_EQ(start_monitor(1, 1), 0);
    CHECK_EQ(start_monitor(-1, 2), 0);
    CHECK_EQ(start_monitor(0, 0), 0);
    CHECK_EQ(start_monitor(3, 0), 0);
}

void test_restored_window()
{
    CHECK_EQ(show(restored_window({ 0, 0 }, { 1800, 1200 })), "300,200 1200x800");
    // A monitor to the right of the main one.
    CHECK_EQ(show(restored_window({ 1800, 0 }, { 2560, 1440 })), "2227,240 1706x960");
    // And one to its left, above it.
    CHECK_EQ(show(restored_window({ -1920, -300 }, { 1920, 1080 })), "-1600,-120 1280x720");
    // Two thirds isn't whole.
    CHECK_EQ(show(restored_window({ 0, 0 }, { 1001, 700 })), "167,117 667x466");
}

}

int main()
{
    test_start_monitor();
    test_restored_window();
    return ST::Test::test_result();
}
