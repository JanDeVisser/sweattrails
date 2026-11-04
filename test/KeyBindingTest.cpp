/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests that a key's binding runs, as a task on the bus, unless a modal was
 * open when the key was pressed.
 */

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

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

// A task whose time it is still doesn't take a key that closed a modal,
// and one that runs under modals still doesn't when its time isn't: Esc
// that closes the month list over the summaries doesn't close them too.
void test_when_under_a_modal()
{
    Test::ScopedTestApp app;
    Bus                 bus;
    bus.add_task<Widget>("test-summaries-close", app.app, [](auto const &, JSONValue const &) { })
        .bind(KeyCombo { KEY_ESCAPE, KModNone })
        .when([] { return true; });
    bus.add_task<Widget>("test-force-quit", app.app, [](auto const &, JSONValue const &) { })
        .bind(KeyCombo { KEY_Q, KModNone })
        .run_under_modals()
        .when([] { return false; });
    bus.handle_keys({ KEY_ESCAPE }, true);
    CHECK(bus.messages.empty());
    bus.handle_keys({ KEY_Q }, true);
    CHECK(bus.messages.empty());
}

// T on its own is the summaries' time metric, Ctrl+T the summaries: T
// doesn't run Ctrl+T's task, bound before it. Without a window no
// modifier is down.
void test_same_key_other_modifier()
{
    Test::ScopedTestApp app;
    Bus                 bus;
    bus.add_task<Widget>("test-summaries", app.app, [](auto const &, JSONValue const &) { })
        .bind(KeyCombo { KEY_T, KModControl });
    bus.add_task<Widget>("test-metric", app.app, [](auto const &, JSONValue const &) { })
        .bind(KeyCombo { KEY_T, KModNone })
        .when([] { return true; });
    bus.handle_keys({ KEY_T }, false);
    CHECK_EQ(task_run(bus), "test-metric");
}

// The arguments of the binding that ran go with the task, also when
// another binding of the same task comes first: W, M and Y are one task.
void test_when_keeps_the_arguments()
{
    Test::ScopedTestApp app;
    Bus                 bus;
    auto               &period = bus.add_task<Widget>("test-period", app.app, [](auto const &, JSONValue const &) { })
                                     .when([] { return true; });
    auto                week = JSONValue::object();
    week.set("period", JSONValue { "week" });
    auto month = JSONValue::object();
    month.set("period", JSONValue { "month" });
    period.bind(KeyCombo { KEY_W, KModNone }, week);
    period.bind(KeyCombo { KEY_M, KModNone }, month);
    bus.handle_keys({ KEY_M }, false);
    CHECK_EQ(bus.messages.size(), 1uz);
    if (bus.messages.size() == 1) {
        auto const &pending = std::get<Bus::PendingTask>(bus.messages.front());
        CHECK_EQ(pending.arguments.try_get<std::string>("period").value_or(""), "month");
    }
}

// The command of `title` among `commands`, if there's one.
std::optional<Command> command(std::vector<Command> const &commands, std::string_view title)
{
    auto const it = std::ranges::find(commands, title, &Command::title);
    if (it == commands.end()) {
        return { };
    }
    return *it;
}

void test_commands()
{
    Test::ScopedTestApp app;
    Bus                 bus;
    auto const          nothing = [](auto const &, JSONValue const &) { };
    bus.add_task<Widget>("test-download", app.app, nothing).titled("Download").bind(KeyCombo { KEY_G, KModControl });
    // Plumbing: no title.
    bus.add_task<Widget>("test-row", app.app, nothing).bind(KeyCombo { KEY_DOWN, KModNone });
    // No key: still a command.
    bus.add_task<Widget>("test-login", app.app, nothing).titled("Log in");
    // Titled bindings are a command each; Enter's other key isn't.
    auto week = JSONValue::object();
    week.set("period", JSONValue { "week" });
    auto month = JSONValue::object();
    month.set("period", JSONValue { "month" });
    bus.add_task<Widget>("test-period", app.app, nothing)
        .titled("Period")
        .bind(KeyCombo { KEY_W, KModNone }, week, "By week")
        .bind(KeyCombo { KEY_M, KModNone }, month, "By month")
        .bind(KeyCombo { KEY_KP_ENTER, KModNone }, week);
    // Not on this screen.
    bus.add_task<Widget>("test-elsewhere", app.app, nothing).titled("Elsewhere").when([] { return false; });
    auto const commands = bus.commands();
    CHECK_EQ(commands.size(), 4uz);
    auto const download = command(commands, "Download");
    CHECK(download && download->task == "test-download" && download->key == (KeyCombo { KEY_G, KModControl }));
    CHECK(download && download->arguments.try_get<int>("key").value_or(0) == KEY_G);
    auto const login = command(commands, "Log in");
    CHECK(login && !login->key && login->arguments.is_null());
    auto const by_month = command(commands, "By month");
    CHECK(by_month && by_month->task == "test-period" && by_month->key == (KeyCombo { KEY_M, KModNone }));
    CHECK(by_month && by_month->arguments.try_get<std::string>("period").value_or("") == "month");
    CHECK(command(commands, "By week").has_value());
    CHECK(!command(commands, "Period").has_value());
    CHECK(!command(commands, "Elsewhere").has_value());
}

void test_key_text()
{
    CHECK_EQ(key_text(KeyCombo { KEY_G, KModControl }), "Ctrl+G");
    CHECK_EQ(key_text(KeyCombo { KEY_F2, KModNone }), "F2");
    CHECK_EQ(key_text(KeyCombo { KEY_ENTER, KModNone }), "Enter");
    CHECK_EQ(key_text(KeyCombo { KEY_ESCAPE, KModNone }), "Esc");
    CHECK_EQ(key_text(KeyCombo { KEY_PAGE_UP, KModNone }), "Page Up");
    CHECK_EQ(key_text(KeyCombo { KEY_KP_ADD, KModNone }), "Keypad +");
    CHECK_EQ(key_text(KeyCombo { KEY_SEVEN, KModShift }), "Shift+7");
#ifdef __APPLE__
    CHECK_EQ(key_text(KeyCombo { KEY_P, KModSuper | KModShift }), "Cmd+Shift+P");
    CHECK_EQ(key_text(KeyCombo { KEY_L, KModControl | KModAlt }), "Ctrl+Option+L");
#else
    CHECK_EQ(key_text(KeyCombo { KEY_P, KModSuper | KModShift }), "Super+Shift+P");
    CHECK_EQ(key_text(KeyCombo { KEY_L, KModControl | KModAlt }), "Ctrl+Alt+L");
#endif
}

}

int main()
{
    test_binding_runs();
    test_no_binding_under_a_modal();
    test_binding_under_a_modal();
    test_when();
    test_when_not_active();
    test_when_under_a_modal();
    test_same_key_other_modifier();
    test_when_keeps_the_arguments();
    test_commands();
    test_key_text();
    return Test::test_result();
}
