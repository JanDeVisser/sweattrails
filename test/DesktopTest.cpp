/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests what the app runs to hand things to the desktop.
 */

#include <string>
#include <vector>

#include <Desktop.h>

#include <TestApp.h>

using namespace ST;
using namespace ST::Test;

namespace {

// What open_browser() and reveal_file() run their programs with.
void test_run_and_wait()
{
    CHECK_EQ(run_and_wait({ "sh", "-c", "exit 0" }).value_or(-1), 0);
    CHECK_EQ(run_and_wait({ "sh", "-c", "exit 3" }).value_or(-1), 3);
    CHECK_EQ(run_and_wait({ "sh", "-c", "sleep 0.1; exit 4" }).value_or(-1), 4);
    // An error on macOS, exit code 127 elsewhere.
    auto const missing = run_and_wait({ "sweattrails-no-such-program" });
    CHECK(!missing.has_value() || *missing != 0);
}

void test_reveal_command()
{
    auto const command = reveal_command("/store/activity/2026/11/1794572226.fit");
#ifdef __APPLE__
    CHECK(command == (std::vector<std::string> { "open", "-R", "/store/activity/2026/11/1794572226.fit" }));
#else
    CHECK(command == (std::vector<std::string> { "xdg-open", "/store/activity/2026/11" }));
#endif
}

}

int main()
{
    test_run_and_wait();
    test_reveal_command();
    return test_result();
}
