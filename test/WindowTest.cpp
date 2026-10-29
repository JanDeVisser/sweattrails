/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string>

#include <Window.h>

#include <TestApp.h>

using namespace ST;

namespace {

std::string show(std::optional<Rectangle> const &window)
{
    if (!window) {
        return "none";
    }
    auto const &r = *window;
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
    // What's left over isn't even, so the window can't be exactly centred.
    CHECK_EQ(show(restored_window({ 0, 0 }, { 1003, 1004 })), "167,167 668x669");
    CHECK_EQ(show(restored_window({ -1003, -1004 }, { 1003, 1004 })), "-836,-837 668x669");
    // A Retina MacBook's screen, in points.
    CHECK_EQ(show(restored_window({ 0, 0 }, { 1512, 982 })), "252,164 1008x654");
}

bool whole(float f)
{
    return std::floor(f) == f;
}

// Whatever the size, from 2, where two thirds is at least 1, the window is
// whole, two thirds of the monitor rounded down, on it, and centred to
// within one.
void test_restored_window_sizes()
{
    for (auto size = 2; size <= 8000; ++size) {
        auto const s = static_cast<float>(size);
        for (auto const position : { -8000.0f, 0.0f, 1800.0f }) {
            auto const window = restored_window({ position, position }, { s, s });
            if (!window) {
                CHECK_EQ(show(window), std::format("size {} at {}", size, position));
                return;
            }
            auto const r = *window;
            auto const left = r.x - position;
            auto const right = position + s - (r.x + r.width);
            if (!whole(r.x) || !whole(r.width) || r.width != static_cast<float>(size * 2 / 3)
                || left < 0 || right < left || right - left > 1 || r.y != r.x || r.height != r.width) {
                CHECK_EQ(show(r), std::format("size {} at {}", size, position));
                return;
            }
        }
    }
}

// A modal has the input, such as the quit prompt, while the window is
// un-maximized: the app still starts each frame, where it picks up the
// resize.
void test_frames_start_under_a_modal()
{
    Test::ScopedTestApp scoped;
    auto               &app = *scoped.app;
    app.push_modal(std::make_shared<Widget>(nullptr));
    auto const before = app.frame_count;
    app.process_input();
    app.process_input();
    CHECK_EQ(app.frame_count, before + 2);
}

// raylib gives a size of 0 for a monitor it can't find the video mode of.
void test_restored_window_no_size()
{
    CHECK_EQ(show(restored_window({ 100, 50 }, { 0, 0 })), "none");
    CHECK_EQ(show(restored_window({ 0, 0 }, { 1920, 0 })), "none");
    CHECK_EQ(show(restored_window({ 0, 0 }, { 1, 1080 })), "none");
}

}

int main()
{
    test_start_monitor();
    test_restored_window();
    test_restored_window_sizes();
    test_restored_window_no_size();
    test_frames_start_under_a_modal();
    return ST::Test::test_result();
}
