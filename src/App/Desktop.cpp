/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <format>

#include <Child.h>
#ifdef _WIN32
#include <shellapi.h>
#include <shlobj.h>

#include <Win32.h>
#endif

#include <Desktop.h>

namespace ST {

#ifndef _WIN32
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
#endif

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

#ifdef _WIN32

std::expected<void, LibCError> open_browser(std::string const &url)
{
    // ShellExecute's result is greater than 32 when it worked.
    auto const result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", to_wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        return std::unexpected(win32_error(static_cast<DWORD>(result)));
    }
    return { };
}

// Explorer, with the file selected. `explorer /select,<file>` would do
// that too, but it quotes by rules of its own, and always exits with 1.
std::expected<void, LibCError> reveal_file(std::filesystem::path const &file)
{
    auto const initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    auto      *item = ILCreateFromPathW(file.c_str());
    auto const result = (item != nullptr) ? SHOpenFolderAndSelectItems(item, 0, nullptr, 0) : E_FAIL;
    if (item != nullptr) {
        ILFree(item);
    }
    if (SUCCEEDED(initialized)) {
        CoUninitialize();
    }
    if (FAILED(result)) {
        return std::unexpected(win32_error(static_cast<DWORD>(result)));
    }
    return { };
}

#else

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

#endif

}
