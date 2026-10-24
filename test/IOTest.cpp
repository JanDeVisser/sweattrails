/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <ctime>
#include <filesystem>
#include <format>
#include <string>

#include <IO.h>

#include <TestApp.h>

using namespace ST;

namespace {

namespace fs = std::filesystem;

struct TempDir {
    fs::path path { fs::temp_directory_path() / std::format("sweattrails-io-{}", std::time(nullptr)) };

    TempDir()
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }

    ~TempDir()
    {
        std::error_code ec { };
        fs::remove_all(path, ec);
    }
};

void test_write_file_atomically()
{
    TempDir    dir;
    auto const file = dir.path / "file.json";
    CHECK(write_file_atomically(file, "{}").has_value());
    CHECK_EQ(read_file_by_name(file.string()).value_or(""), "{}");
    CHECK(!fs::exists(dir.path / "file.json.part"));

    // An existing file is replaced.
    CHECK(write_file_atomically(file, "[]").has_value());
    CHECK_EQ(read_file_by_name(file.string()).value_or(""), "[]");
}

void test_write_file_atomically_with_permissions()
{
    TempDir    dir;
    auto const file = dir.path / "secret.json";
    auto const owner_only = fs::perms::owner_read | fs::perms::owner_write;
    CHECK(write_file_atomically(file, "secret", owner_only).has_value());
    CHECK(fs::status(file).permissions() == owner_only);
    CHECK_EQ(read_file_by_name(file.string()).value_or(""), "secret");
}

void test_write_file_atomically_fails_cleanly()
{
    TempDir    dir;
    auto const file = dir.path / "missing" / "file.json";
    auto const written = write_file_atomically(file, "{}");
    CHECK(!written.has_value());
    CHECK(!fs::exists(dir.path / "missing"));
}

}

int main()
{
    test_write_file_atomically();
    test_write_file_atomically_with_permissions();
    test_write_file_atomically_fails_cleanly();
    return ST::Test::test_result();
}
