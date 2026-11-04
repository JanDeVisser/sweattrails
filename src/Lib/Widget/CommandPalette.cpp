/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cctype>

#include <CommandPalette.h>

namespace ST {

void RecentCommands::used(std::string_view title)
{
    std::erase(titles, title);
    titles.insert(titles.begin(), std::string { title });
    if (titles.size() > Count) {
        titles.resize(Count);
    }
}

namespace {

bool before_by_title(Command const &a, Command const &b)
{
    return std::ranges::lexicographical_compare(a.title, b.title, [](unsigned char x, unsigned char y) {
        return std::tolower(x) < std::tolower(y);
    });
}

}

std::vector<Command> order_commands(std::vector<Command> commands, std::vector<std::string> const &recent)
{
    std::vector<Command> ret;
    for (auto const &title : recent) {
        if (auto const it = std::ranges::find(commands, title, &Command::title); it != commands.end()) {
            ret.push_back(*it);
            commands.erase(it);
        }
    }
    std::ranges::stable_sort(commands, before_by_title);
    ret.append_range(commands);
    return ret;
}

CommandPalette::CommandPalette(std::vector<Command> commands, Run run)
    : CommandList("Command")
    , commands(std::move(commands))
    , run(std::move(run))
{
    for (auto ix = 0uz; ix < this->commands.size(); ++ix) {
        auto const &command = this->commands[ix];
        auto       &entry = entries.emplace_back(command.title, ix);
        entry.columns = { command.key ? key_text(*command.key) : std::string { } };
    }
}

void CommandPalette::submit()
{
    run(commands[selected().payload]);
}

void command_palette(std::vector<Command> const &commands, RecentCommands const &recent, CommandPalette::Run run)
{
    auto const &palette = Widget::make<CommandPalette>(order_commands(commands, recent.titles), std::move(run));
    palette->show();
}

}
