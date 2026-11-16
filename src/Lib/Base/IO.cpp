/*
 * Copyright (c) 2023, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cerrno>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>

#include <config.h>
#include <pwd.h>
#include <unistd.h>

#include <Error.h>
#include <Expected.h>
#include <IO.h>

namespace ST {

std::filesystem::path home_dir()
{
    if (char const *home = getenv("HOME"); home != nullptr && *home != 0) {
        return home;
    }
    if (passwd const *pw = getpwuid(getuid()); pw != nullptr) {
        return pw->pw_dir;
    }
    return ".";
}

std::expected<bool, LibCError> path_exists(std::filesystem::path const &path)
{
    std::error_code ec { };
    auto const      ret = std::filesystem::exists(path, ec);
    if (ec) {
        return std::unexpected(LibCError { ec });
    }
    return ret;
}

std::expected<std::filesystem::path, LibCError> ensure_directory(std::filesystem::path const &dir)
{
    std::error_code ec { };
    if (!std::filesystem::is_directory(dir, ec)) {
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            return std::unexpected(LibCError { ec });
        }
    }
    return dir;
}

namespace {

#ifndef IS_APPLE
std::filesystem::path xdg_dir(char const *env_var, std::filesystem::path const &fallback)
{
    // The XDG spec says relative paths in these variables are to be ignored.
    if (char const *dir = getenv(env_var); dir != nullptr && *dir == '/') {
        return dir;
    }
    return home_dir() / fallback;
}
#endif

}

std::expected<void, LibCError> write_file_atomically(std::filesystem::path const &path, std::string_view contents, std::optional<std::filesystem::perms> permissions)
{
    namespace fs = std::filesystem;
    auto const      part = fs::path { path.string() + ".part" };
    std::error_code ec { };
    auto const      fail = [&part](std::string_view what, LibCError const &error) -> std::unexpected<LibCError> {
        std::error_code ignored { };
        fs::remove(part, ignored);
        return std::unexpected(LibCError { std::format("Could not {} `{}`: {}", what, part.string(), error.to_string()) });
    };
    if (permissions) {
        // Created empty, so that it has the permissions before it has the
        // contents.
        if (auto const created = write_file_by_name(part.string(), std::string_view { }); !created) {
            return fail("write", created.error());
        }
        fs::permissions(part, *permissions, ec);
        if (ec) {
            return fail("set the permissions of", LibCError { ec });
        }
    }
    if (auto const written = write_file_by_name(part.string(), contents); !written) {
        return fail("write", written.error());
    }
    fs::rename(part, path, ec);
    if (ec) {
        return fail("rename", LibCError { ec });
    }
    return { };
}

std::filesystem::path user_config_dir(std::string_view const &app)
{
#ifdef IS_APPLE
    return home_dir() / "Library" / "Application Support" / app;
#else
    return xdg_dir("XDG_CONFIG_HOME", ".config") / app;
#endif
}

std::filesystem::path user_data_dir(std::string_view const &app)
{
#ifdef IS_APPLE
    return home_dir() / "Library" / "Application Support" / app;
#else
    return xdg_dir("XDG_DATA_HOME", std::filesystem::path { ".local" } / "share") / app;
#endif
}

std::filesystem::path user_state_dir(std::string_view const &app)
{
#ifdef IS_APPLE
    return home_dir() / "Library" / "Application Support" / app;
#else
    return xdg_dir("XDG_STATE_HOME", std::filesystem::path { ".local" } / "state") / app;
#endif
}

}
