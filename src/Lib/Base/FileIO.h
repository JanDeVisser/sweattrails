/*
 * Copyright (c) 2023, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <Error.h>
#include <Expected.h>

namespace ST {

// Files are read and written as they are, in binary: in text mode, Windows
// turns \n into \r\n, and stops reading at a ^Z.
template<typename T = char>
std::expected<std::basic_string<T>, LibCError> read_file_by_name(std::string_view const &file_name)
{
    std::ifstream is(std::string { file_name }, std::ios::binary);
    if (!is) {
        return std::unexpected(LibCError());
    }
    std::string ret;
    for (char ch; is.get(ch);) {
        ret += ch;
    }
    return ret;
}

template<typename T = char>
std::expected<ssize_t, LibCError> write_file_by_name(std::string_view const &file_name, std::basic_string_view<T> const &contents)
{
    std::basic_ofstream<T> os(std::string { file_name }, std::ios::binary);
    if (!os) {
        return std::unexpected(LibCError());
    }
    os.write(contents.data(), contents.length());
    if (os.fail() || os.bad()) {
        return std::unexpected(LibCError());
    }
    return contents.length();
}

template<typename T = char>
std::expected<ssize_t, LibCError> write_file_by_name(std::string_view const &file_name, std::basic_string<T> const &contents)
{
    return write_file_by_name(file_name, std::basic_string_view<T> { contents });
}

// Writes `contents` to `path` by way of `<path>.part`, which is renamed to
// `path` when it's complete, so whoever watches `path` never sees half a
// file. With `permissions`, the `.part` gets them before anything is
// written to it, so that, for example, a file only the user may read never
// is readable by others, if only for a moment.
std::expected<void, LibCError> write_file_atomically(std::filesystem::path const &path, std::string_view contents, std::optional<std::filesystem::perms> permissions = { });

// The user's home directory: $HOME, or the password database entry if HOME
// isn't set. On Windows, the user's profile folder.
std::filesystem::path home_dir();

// std::filesystem::exists(), returning errors instead of throwing them.
std::expected<bool, LibCError> path_exists(std::filesystem::path const &path);

// Creates `dir`, and any missing parent directories, if it doesn't exist yet.
std::expected<std::filesystem::path, LibCError> ensure_directory(std::filesystem::path const &dir);

// Per-user directories for application `app`, following the platform's
// conventions. On macOS all three are ~/Library/Application Support/<app>.
// On Windows the config directory is %APPDATA%\<app>, which roams with the
// user's profile, and the other two %LOCALAPPDATA%\<app>, which doesn't:
// the activity store is too big for that. Elsewhere they are the XDG base
// directories: $XDG_CONFIG_HOME (default
// ~/.config), $XDG_DATA_HOME (~/.local/share) and $XDG_STATE_HOME
// (~/.local/state), each followed by <app>. The directories are not created.
std::filesystem::path user_config_dir(std::string_view const &app);
std::filesystem::path user_data_dir(std::string_view const &app);
std::filesystem::path user_state_dir(std::string_view const &app);

// The running program's file.
std::filesystem::path executable_path();

// Where an installed app keeps its data, for the program `executable`:
// share/ in a macOS bundle's Resources, or share\ next to the program on
// Windows, if that is a directory. Otherwise `fallback`, such as the build
// directory's share/ during development.
std::filesystem::path find_data_dir(std::filesystem::path const &executable, std::filesystem::path const &fallback);

}
