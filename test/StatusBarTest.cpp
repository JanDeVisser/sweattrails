/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests what StatusBar shows: a message on its own, or the idle text and
 * the hint. draw() needs raylib, so the tests ask shown().
 */

#include <memory>

#include <Widget.h>

#include <TestApp.h>

using namespace ST;

namespace {

std::shared_ptr<StatusBar> idle_bar()
{
    auto bar = std::make_shared<StatusBar>(nullptr);
    bar->idle = "12 activities, the newest on Thu 12 Nov 2026";
    bar->hint = "Cmd+Shift+P for commands";
    return bar;
}

// Without a message: the idle text on the left, the hint on the right.
void test_idle()
{
    auto const bar = idle_bar();
    auto const shown = bar->shown();
    CHECK_EQ(shown.left, "12 activities, the newest on Thu 12 Nov 2026");
    CHECK_EQ(shown.right, "Cmd+Shift+P for commands");
}

// A message replaces both, so that a long one has the whole bar.
void test_message()
{
    auto const bar = idle_bar();
    bar->message = "Reading activities for the totals: 3 of 40";
    auto const shown = bar->shown();
    CHECK_EQ(shown.left, "Reading activities for the totals: 3 of 40");
    CHECK(shown.right.empty());
}

// Clearing the message goes back to the idle text.
void test_cleared()
{
    auto const bar = idle_bar();
    bar->message = "Grouped 2 activities recorded more than once";
    bar->message.clear();
    CHECK_EQ(bar->shown().left, "12 activities, the newest on Thu 12 Nov 2026");
    CHECK_EQ(bar->shown().right, "Cmd+Shift+P for commands");
}

// Before there's an idle text, only the hint shows.
void test_no_idle_yet()
{
    auto const bar = idle_bar();
    bar->idle.clear();
    CHECK(bar->shown().left.empty());
    CHECK_EQ(bar->shown().right, "Cmd+Shift+P for commands");
}

// Always one line, whatever it shows.
void test_always_a_line()
{
    auto const bar = idle_bar();
    CHECK(bar->policy == SizePolicy::Characters);
    CHECK_EQ(bar->policy_size, 1.0f);
}

}

int main()
{
    Test::ScopedTestApp app;
    test_idle();
    test_message();
    test_cleared();
    test_no_idle_yet();
    test_always_a_line();
    return Test::test_result();
}
