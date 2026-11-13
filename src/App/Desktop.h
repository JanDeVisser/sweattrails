/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Handing things to the desktop: a URL to the browser, a file to Finder
 * (or the file manager off macOS).
 */

#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include <Error.h>

namespace ST {

// Runs `command`, the program and its arguments, looking for the program
// on the PATH, and waits for it to end. Returns its exit code.
std::expected<int, LibCError> run_and_wait(std::vector<std::string> const &command);

// Opens `url` in the user's browser.
std::expected<void, LibCError> open_browser(std::string const &url);

// What shows `file` in the file manager: Finder, with the file selected,
// on macOS; elsewhere, xdg-open on its folder, which can't select it.
std::vector<std::string> reveal_command(std::filesystem::path const &file);

// Shows `file` in the file manager, with reveal_command().
std::expected<void, LibCError> reveal_file(std::filesystem::path const &file);

}
