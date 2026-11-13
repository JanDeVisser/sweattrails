/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cerrno>
#include <format>

#include <spawn.h>
#include <sys/wait.h>

#include <Desktop.h>

// The environment, for the programs run_and_wait() runs: POSIX has no
// header that declares it.
extern char **environ;

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
    // Not Process: its pipes' reader threads outlive it.
    std::vector<char *> argv;
    for (auto const &argument : command) {
        argv.push_back(const_cast<char *>(argument.c_str()));
    }
    argv.push_back(nullptr);
    pid_t pid { 0 };
    if (auto const error = ::posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ); error != 0) {
        return std::unexpected(LibCError { error });
    }
    int status { 0 };
    while (::waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) {
            return std::unexpected(LibCError { });
        }
    }
    if (!WIFEXITED(status)) {
        return std::unexpected(LibCError { std::format("`{}` ended with signal {}", command.front(), WTERMSIG(status)) });
    }
    return WEXITSTATUS(status);
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
