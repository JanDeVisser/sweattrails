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

// Force quit gets past a modal, such as "Are you sure you want to quit?".
void test_binding_under_a_modal()
{
    Test::ScopedTestApp app;
    Bus                 bus;
    bus.add_task<Widget>("test-force-quit", app.app, [](auto const &, JSONValue const &) { })
        .bind(KeyCombo { KEY_Q, KModNone })
        .run_under_modals();
    bind_key(bus, app.app, "test-escape", KEY_ESCAPE);
    bus.handle_keys({ KEY_ESCAPE }, true);
    CHECK(bus.messages.empty());
    bus.handle_keys({ KEY_Q }, true);
    CHECK_EQ(bus.messages.size(), 1uz);
}

// The task a key ran, by name.
std::string task_run(Bus const &bus)
{
    if (bus.messages.size() != 1) {
        return "";
    }
    return std::get<Bus::PendingTask>(bus.messages.front()).task.task;
}

// Two tasks on the same key, each for another time: the key goes to the
// one whose time it is, whichever was bound first.
void test_when()
{
    Test::ScopedTestApp app;
    auto                summaries = false;
    for (auto summaries_first : { true, false }) {
        Bus  bus;
        auto bind_summaries = [&] {
            bus.add_task<Widget>("test-summaries-left", app.app, [](auto const &, JSONValue const &) { })
                .bind(KeyCombo { KEY_LEFT, KModNone })
                .when([&summaries] { return summaries; });
        };
        auto bind_activity = [&] {
            bus.add_task<Widget>("test-previous-activity", app.app, [](auto const &, JSONValue const &) { })
                .bind(KeyCombo { KEY_LEFT, KModNone })
                .when([&summaries] { return !summaries; });
        };
        if (summaries_first) {
            bind_summaries();
            bind_activity();
        } else {
            bind_activity();
            bind_summaries();
        }
        summaries = false;
        bus.handle_keys({ KEY_LEFT }, false);
        CHECK_EQ(task_run(bus), "test-previous-activity");
        bus.messages.clear();
        summaries = true;
        bus.handle_keys({ KEY_LEFT }, false);
        CHECK_EQ(task_run(bus), "test-summaries-left");
    }
}

// A task whose time it isn't doesn't take its key, and nothing else does.
void test_when_not_active()
{
    Test::ScopedTestApp app;
    Bus                 bus;
    bus.add_task<Widget>("test-summaries-left", app.app, [](auto const &, JSONValue const &) { })
        .bind(KeyCombo { KEY_LEFT, KModNone })
        .when([] { return false; });
    bus.handle_keys({ KEY_LEFT }, false);
    CHECK(bus.messages.empty());
}

}

int main()
{
    test_binding_runs();
    test_no_binding_under_a_modal();
    test_binding_under_a_modal();
    test_when();
    test_when_not_active();
    return Test::test_result();
}
