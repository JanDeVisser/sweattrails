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
#ifndef _WIN32
#include <pwd.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <Error.h>
#include <Expected.h>
#include <FileIO.h>
#ifdef _WIN32
#include <Win32.h>
#endif

namespace ST {

#ifdef _WIN32
namespace {

// A folder Windows knows, such as FOLDERID_Profile, the user's home.
std::filesystem::path known_folder(KNOWNFOLDERID const &id)
{
    wchar_t              *path { nullptr };
    std::filesystem::path ret { "." };
    if (SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path) == S_OK) {
        ret = path;
    }
    CoTaskMemFree(path);
    return ret;
}

}

std::filesystem::path home_dir()
{
    return known_folder(FOLDERID_Profile);
}
#else
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
#endif

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

#ifdef IS_LINUX
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
#if defined(IS_WINDOWS)
    return known_folder(FOLDERID_RoamingAppData) / app;
#elif defined(IS_APPLE)
    return home_dir() / "Library" / "Application Support" / app;
#else
    return xdg_dir("XDG_CONFIG_HOME", ".config") / app;
#endif
}

std::filesystem::path user_data_dir(std::string_view const &app)
{
#if defined(IS_WINDOWS)
    return known_folder(FOLDERID_LocalAppData) / app;
#elif defined(IS_APPLE)
    return home_dir() / "Library" / "Application Support" / app;
#else
    return xdg_dir("XDG_DATA_HOME", std::filesystem::path { ".local" } / "share") / app;
#endif
}

std::filesystem::path user_state_dir(std::string_view const &app)
{
#if defined(IS_WINDOWS)
    return known_folder(FOLDERID_LocalAppData) / app;
#elif defined(IS_APPLE)
    return home_dir() / "Library" / "Application Support" / app;
#else
    return xdg_dir("XDG_STATE_HOME", std::filesystem::path { ".local" } / "state") / app;
#endif
}

std::filesystem::path executable_path()
{
#if defined(IS_WINDOWS)
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        auto const length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
#elif defined(IS_APPLE)
    uint32_t size { 0 };
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    _NSGetExecutablePath(path.data(), &size);
    path.resize(path.find('\0'));
    // The path the program was started with, which can be relative or go
    // through a link.
    std::error_code ec { };
    auto            canonical = std::filesystem::weakly_canonical(path, ec);
    return ec ? std::filesystem::path { path } : canonical;
#else
    std::error_code ec { };
    return std::filesystem::read_symlink("/proc/self/exe", ec);
#endif
}

std::filesystem::path find_data_dir(std::filesystem::path const &executable, std::filesystem::path const &fallback)
{
    // Without it, the candidates would be in the current directory.
    if (executable.empty()) {
        return fallback;
    }
    auto const dir = executable.parent_path();
    for (auto const &candidate : { dir / ".." / "Resources" / "share", dir / "share" }) {
        std::error_code ec { };
        if (std::filesystem::is_directory(candidate, ec)) {
            return candidate.lexically_normal();
        }
    }
    return fallback;
}

}
