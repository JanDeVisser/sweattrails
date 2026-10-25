/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests that a key's binding runs, as a task on the bus, unless a modal was
 * open when the key was pressed.
 */

#include <memory>

#include <App.h>

#include <TestApp.h>

using namespace ST;

namespace {

// The bus only runs these, and these tests don't run the bus.
struct NoJob {
    void execute() const { }
};

struct NoNotification {
    void execute() const { }
};

using Bus = AppBus<NoJob, NoNotification>;

void bind_key(Bus &bus, pWidget const &owner, std::string_view task, int key)
{
    bus.add_task<Widget>(task, owner, [](auto const &, JSONValue const &) { });
    bus.bind(task, KeyCombo { key, KModNone });
}

void test_binding_runs()
{
    Test::ScopedTestApp app;
    Bus                 bus;
    bind_key(bus, app.app, "test-down", KEY_DOWN);
    bus.handle_keys({ KEY_DOWN }, false);
    CHECK_EQ(bus.messages.size(), 1uz);
    // Another key: nothing.
    bus.handle_keys({ KEY_UP }, false);
    CHECK_EQ(bus.messages.size(), 1uz);
}

// Esc that closes a modal shouldn't also run Esc's binding.
void test_no_binding_under_a_modal()
{
    Test::ScopedTestApp app;
    Bus                 bus;
    bind_key(bus, app.app, "test-escape", KEY_ESCAPE);
    bus.handle_keys({ KEY_ESCAPE }, true);
    CHECK(bus.messages.empty());
}

}

int main()
{
    test_binding_runs();
    test_no_binding_under_a_modal();
    return Test::test_result();
}
