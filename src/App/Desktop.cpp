/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <format>

#include <Child.h>

#include <Desktop.h>

namespace ST {

namespace {

// Runs `command`, and an exit code other than 0 is an error.
std::expected<void, LibCError> run_to_success(std::vector<std::string> const &command)
{
    auto const exit_code = run_and_wait(command);
    if (!exit_code) {
        return std::unexpected(exit_code.error());
    }
    if (*exit_code != 0) {
        return std::unexpected(LibCError { std::format("exit code {}", *exit_code) });
    }
    return { };
}

}

std::expected<int, LibCError> run_and_wait(std::vector<std::string> const &command)
{
    auto child = Child::start(command, Child::Options { });
    if (!child) {
        return std::unexpected(child.error());
    }
    auto const exit_code = child->wait();
    if (!exit_code) {
        return std::unexpected(LibCError { std::format("`{}` {}", command.front(), exit_code.error().description) });
    }
    return *exit_code;
}

std::expected<void, LibCError> open_browser(std::string const &url)
{
#ifdef __APPLE__
    return run_to_success({ "open", url });
#else
    return run_to_success({ "xdg-open", url });
#endif
}

std::vector<std::string> reveal_command(std::filesystem::path const &file)
{
#ifdef __APPLE__
    return { "open", "-R", file.string() };
#else
    return { "xdg-open", file.parent_path().string() };
#endif
}

std::expected<void, LibCError> reveal_file(std::filesystem::path const &file)
{
    return run_to_success(reveal_command(file));
}

}
