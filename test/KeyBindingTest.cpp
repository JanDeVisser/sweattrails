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

// As the app's W, M and Y: a task without a title of its own, only on the
// summaries, whose bindings have titles. Its untitled binding, as the
// keypad's Enter next to Enter, is no entry, and it has no entry under
// its id.
void test_commands_of_titled_bindings()
{
    Test::ScopedTestApp app;
    auto                summaries = true;
    Bus                 bus;
    auto const          nothing = [](auto const &, JSONValue const &) { };
    auto                week = JSONValue::object();
    week.set("period", JSONValue { "week" });
    auto year = JSONValue::object();
    year.set("period", JSONValue { "year" });
    auto &period = bus.add_task<Widget>("test-period", app.app, nothing).when([&summaries] { return summaries; });
    period.bind(KeyCombo { KEY_W, KModNone }, week, "By week");
    period.bind(KeyCombo { KEY_KP_1, KModNone }, week);
    period.bind(KeyCombo { KEY_Y, KModNone }, year, "By year");
    auto commands = bus.commands();
    CHECK_EQ(commands.size(), 2uz);
    auto const by_year = command(commands, "By year");
    CHECK(by_year && by_year->key == (KeyCombo { KEY_Y, KModNone }));
    CHECK(by_year && by_year->arguments.try_get<std::string>("period").value_or("") == "year");
    CHECK(!command(commands, "").has_value());
    // On the activity screen: none of them.
    summaries = false;
    CHECK(bus.commands().empty());
    // Its titled keys still run it, with their arguments.
    summaries = true;
    bus.handle_keys({ KEY_Y }, false);
    CHECK_EQ(task_run(bus), "test-period");
    if (bus.messages.size() == 1) {
        CHECK_EQ(std::get<Bus::PendingTask>(bus.messages.front()).arguments.try_get<std::string>("period").value_or(""), "year");
    }
}

// A titled task with several keys is one entry, with its first key, as
// zoom in's = and the keypad's +.
void test_commands_first_key()
{
    Test::ScopedTestApp app;
    Bus                 bus;
    bus.add_task<Widget>("test-zoom-in", app.app, [](auto const &, JSONValue const &) { })
        .titled("Zoom in")
        .bind(KeyCombo { KEY_EQUAL, KModNone })
        .bind(KeyCombo { KEY_EQUAL, KModShift })
        .bind(KeyCombo { KEY_KP_ADD, KModNone })
        .when([] { return true; });
    auto const commands = bus.commands();
    CHECK_EQ(commands.size(), 1uz);
    auto const zoom = command(commands, "Zoom in");
    CHECK(zoom && zoom->key == (KeyCombo { KEY_EQUAL, KModNone }));
    // The arguments the key would have run it with.
    CHECK(zoom && zoom->arguments.try_get<int>("key").value_or(0) == KEY_EQUAL);
    CHECK(zoom && zoom->arguments.try_get<int>("modifier").value_or(-1) == KModNone);
}

// The arrows, the keypad, and the modifiers in order.
void test_key_text_more()
{
    CHECK_EQ(key_text(KeyCombo { KEY_LEFT, KModNone }), "Left");
    CHECK_EQ(key_text(KeyCombo { KEY_RIGHT, KModShift }), "Shift+Right");
    CHECK_EQ(key_text(KeyCombo { KEY_UP, KModNone }), "Up");
    CHECK_EQ(key_text(KeyCombo { KEY_DOWN, KModNone }), "Down");
    CHECK_EQ(key_text(KeyCombo { KEY_KP_ENTER, KModNone }), "Keypad Enter");
    CHECK_EQ(key_text(KeyCombo { KEY_KP_1, KModNone }), "Keypad 1");
    CHECK_EQ(key_text(KeyCombo { KEY_F12, KModNone }), "F12");
    CHECK_EQ(key_text(KeyCombo { KEY_A, KModNone }), "A");
    CHECK_EQ(key_text(KeyCombo { KEY_Z, KModNone }), "Z");
    CHECK_EQ(key_text(KeyCombo { KEY_ZERO, KModNone }), "0");
    CHECK_EQ(key_text(KeyCombo { KEY_EQUAL, KModShift }), "Shift+=");
    // All four, in the README's order, whatever order they're or-ed in.
#ifdef __APPLE__
    CHECK_EQ(key_text(KeyCombo { KEY_X, KModShift | KModAlt | KModControl | KModSuper }), "Cmd+Ctrl+Option+Shift+X");
    CHECK_EQ(key_text(KeyCombo { KEY_R, KModControl | KModShift }), "Ctrl+Shift+R");
#else
    CHECK_EQ(key_text(KeyCombo { KEY_X, KModShift | KModAlt | KModControl | KModSuper }), "Super+Ctrl+Alt+Shift+X");
#endif
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
    test_commands_of_titled_bindings();
    test_commands_first_key();
    test_key_text_more();
    return Test::test_result();
}
