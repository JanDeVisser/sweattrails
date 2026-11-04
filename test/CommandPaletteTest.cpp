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

}

int main()
{
    test_order();
    test_recent();
    test_run_a_command();
    test_escape_runs_nothing();
    test_no_match_runs_nothing();
    return Test::test_result();
}
