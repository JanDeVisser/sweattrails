/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The command palette: the commands that work now, by title, with their
 * keys, to find by typing and run with Enter, as in VS Code and Zed.
 */

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <Modal.h>
#include <Widget.h>

namespace ST {

// The titles of the commands last run from the palette, the most recent
// first, for as long as the app runs.
struct RecentCommands {
    static constexpr size_t Count = 5;

    std::vector<std::string> titles { };

    void used(std::string_view title);
};

// The recent commands first, the most recent at the top, then the rest
// by title, A to Z, whatever the case.
[[nodiscard]] std::vector<Command> order_commands(std::vector<Command> commands, std::vector<std::string> const &recent);

// An entry is the index of its command: the search goes by its title, and
// the order is order_commands()'s.
using CommandList = ListBox<size_t, ListBoxConfig { .search = true, .sort = false }>;

struct CommandPalette : public CommandList {
    using Run = std::function<void(Command const &)>;

    std::vector<Command> commands;
    Run                  run;

    // The commands in the order they're listed.
    CommandPalette(std::vector<Command> commands, Run run);
    void submit() override;
};

// Opens the palette on `commands`, the recent ones first, which on Enter
// calls `run` with the selected one.
void command_palette(std::vector<Command> const &commands, RecentCommands const &recent, CommandPalette::Run run);

}
