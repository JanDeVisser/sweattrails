/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <filesystem>
#include <string>

#include <IO.h>

#include <TempDir.h>
#include <TestApp.h>

using namespace ST;
using namespace ST::Test;

namespace {

namespace fs = std::filesystem;

void test_write_file_atomically()
{
    TempDir    dir { "io" };
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
    TempDir    dir { "io" };
    auto const file = dir.path / "secret.json";
    auto const owner_only = fs::perms::owner_read | fs::perms::owner_write;
    CHECK(write_file_atomically(file, "secret", owner_only).has_value());
    CHECK(fs::status(file).permissions() == owner_only);
    CHECK_EQ(read_file_by_name(file.string()).value_or(""), "secret");
}

void test_write_file_atomically_fails_cleanly()
{
    TempDir    dir { "io" };
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
