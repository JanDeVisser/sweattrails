/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <ctime>
#include <filesystem>
#include <format>
#include <string_view>
#include <system_error>

#include <unistd.h>

namespace ST::Test {

// A directory for one test, removed when the test is done. The process ID
// keeps tests that ctest runs side by side, with `-j`, out of each other's
// directories.
struct TempDir {
    std::filesystem::path path;

    explicit TempDir(std::string_view name)
        : path(std::filesystem::temp_directory_path() / std::format("sweattrails-{}-{}-{}", name, ::getpid(), std::time(nullptr)))
    {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~TempDir()
    {
        std::error_code ec { };
        std::filesystem::remove_all(path, ec);
    }

    TempDir(TempDir const &) = delete;
    TempDir &operator=(TempDir const &) = delete;
};

}
