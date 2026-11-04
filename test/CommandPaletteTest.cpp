/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the command palette's order, its recent commands, and running a
 * command found by typing, by feeding it keys and characters.
 */

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <CommandPalette.h>

#include <TestApp.h>

using namespace ST;

namespace {

Command command(std::string title, std::optional<KeyCombo> key = { })
{
    return Command { .title = title, .task = std::format("test-{}", title), .key = key };
}

std::vector<std::string> titles(std::vector<Command> const &commands)
{
    std::vector<std::string> ret;
    for (auto const &c : commands) {
        ret.push_back(c.title);
    }
    return ret;
}

void test_order()
{
    std::vector<Command> const commands { command("rename"), command("Download"), command("Quit"), command("compare") };
    CHECK(titles(order_commands(commands, { })) == (std::vector<std::string> { "compare", "Download", "Quit", "rename" }));
    // The recent first, the most recent at the top; one that isn't on this
    // screen is left out.
    CHECK(titles(order_commands(commands, { "Quit", "Elsewhere", "compare" })) == (std::vector<std::string> { "Quit", "compare", "Download", "rename" }));
}

void test_recent()
{
    RecentCommands recent;
    for (auto const *title : { "a", "b", "c", "d", "e", "f" }) {
        recent.used(title);
    }
    CHECK(recent.titles == (std::vector<std::string> { "f", "e", "d", "c", "b" }));
    recent.used("c");
    CHECK(recent.titles == (std::vector<std::string> { "c", "f", "e", "d", "b" }));
}

struct Palette {
    std::optional<Command>          ran { };
    std::shared_ptr<CommandPalette> palette;

    explicit Palette(std::vector<Command> const &commands)
        : palette(std::make_shared<CommandPalette>(order_commands(commands, { }), [this](Command const &c) { ran = c; }))
    {
        // What show() does, but resize(), which needs raylib.
        palette->filter();
        palette->selection = 0;
        palette->scroll.lines = 20;
        palette->status = ModalStatus::Active;
    }

    void type(std::string_view text)
    {
        for (auto const ch : text) {
            palette->character(ch);
        }
    }
};

void test_run_a_command()
{
    Test::ScopedTestApp app;
    Palette             p { { command("Download from Garmin Connect", KeyCombo { KEY_G, KModControl }), command("Download from Wahoo"),
        command("Rename activity") } };
    // Its key at the row's end; none for a command without one.
    CHECK(p.palette->entries[0].columns == (std::vector<std::string> { "Ctrl+G" }));
    CHECK(p.palette->entries[1].columns == (std::vector<std::string> { "" }));
    p.type("wahoo down");
    CHECK_EQ(p.palette->shown_count(), 1);
    p.palette->process_key(KModNone, KEY_ENTER);
    CHECK(p.ran && p.ran->title == "Download from Wahoo");
    CHECK(p.palette->status == ModalStatus::Submitted);
}

void test_escape_runs_nothing()
{
    Test::ScopedTestApp app;
    Palette             p { { command("Quit") } };
    p.palette->process_key(KModNone, KEY_ESCAPE);
    CHECK(!p.ran);
    CHECK(p.palette->status == ModalStatus::Dismissed);
}

void test_no_match_runs_nothing()
{
    Test::ScopedTestApp app;
    Palette             p { { command("Quit") } };
    p.type("zz");
    p.palette->process_key(KModNone, KEY_ENTER);
    CHECK(!p.ran);
}

// An entry is its command's index in the palette's list, not in the
// matches: with a search typed, Down and Enter run the second match.
void test_runs_the_selected_match()
{
    Test::ScopedTestApp        app;
    std::vector<Command> const commands { command("Quit"), command("Totals by year"), command("Totals by week"), command("Totals by month") };
    {
        // Listed A to Z: Quit, month, week, year.
        Palette p { commands };
        p.type("totals by");
        CHECK_EQ(p.palette->shown_count(), 3);
        p.palette->process_key(KModNone, KEY_DOWN);
        p.palette->process_key(KModNone, KEY_ENTER);
        CHECK(p.ran && p.ran->title == "Totals by week");
    }
    {
        Palette p { commands };
        p.type("year");
        p.palette->process_key(KModNone, KEY_ENTER);
        CHECK(p.ran && p.ran->title == "Totals by year");
    }
    {
        // A search typed and taken back: the whole list again, from its top.
        Palette p { commands };
        p.type("w");
        p.palette->process_key(KModNone, KEY_BACKSPACE);
        p.palette->process_key(KModNone, KEY_DOWN);
        p.palette->process_key(KModNone, KEY_ENTER);
        CHECK(p.ran && p.ran->title == "Totals by month");
    }
}

void test_empty_palette()
{
    Test::ScopedTestApp app;
    Palette             p { { } };
    p.palette->process_key(KModNone, KEY_DOWN);
    p.palette->process_key(KModNone, KEY_ENTER);
    CHECK(!p.ran);
    p.type("q");
    p.palette->process_key(KModNone, KEY_ENTER);
    CHECK(!p.ran);
}

// The bus only runs these, and these tests don't run its jobs.
struct NoJob {
    void execute() const { }
};

struct NoNotification {
    void execute() const { }
};

// As the app runs a command: Enter submits its task, and the palette
// closes; the task runs when the bus next handles a message, with no
// modal open, so that a handler that refuses under a modal, as most of
// the app's do, still runs. The focus is the one from before the palette.
void test_task_runs_after_the_palette_closes()
{
    Test::ScopedTestApp           app;
    AppBus<NoJob, NoNotification> bus;
    std::optional<bool>           modal_open { };
    JSONValue                     arguments { };
    auto const                    focus = std::make_shared<Widget>(app.app);
    app.app->focus = focus;
    bus.add_task<Widget>("test-rename", app.app, [&](auto const &, JSONValue const &args) {
        modal_open = !App::the()->modals.empty();
        arguments = args;
        // As a handler that opens an input box would move the focus.
        App::the()->focus = nullptr;
    });
    auto year = JSONValue::object();
    year.set("period", JSONValue { "year" });
    auto const palette = std::make_shared<CommandPalette>(std::vector<Command> { Command { .title = "Rename", .task = "test-rename", .arguments = year } },
        [&bus](Command const &c) { bus.submit(c.task, c.arguments); });
    palette->filter();
    palette->scroll.lines = 20;
    palette->make_visible();
    CHECK_EQ(app.app->modals.size(), 1uz);
    palette->process_key(KModNone, KEY_ENTER);
    CHECK(app.app->modals.empty());
    CHECK(!modal_open);
    bus.handle_one();
    CHECK(modal_open.has_value() && !*modal_open);
    CHECK_EQ(arguments.try_get<std::string>("period").value_or(""), "year");
    CHECK(app.app->focus == focus);
}

}

int main()
{
    test_order();
    test_recent();
    test_run_a_command();
    test_escape_runs_nothing();
    test_no_match_runs_nothing();
    test_runs_the_selected_match();
    test_empty_palette();
    test_task_runs_after_the_palette_closes();
    return Test::test_result();
}
