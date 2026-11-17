/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <filesystem>
#include <string>

#include <Environment.h>
#include <FileIO.h>

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
#ifndef _WIN32
    // Windows has no permissions for group and others.
    CHECK(fs::status(file).permissions() == owner_only);
#endif
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

// A name that isn't ASCII: on Windows that takes the UTF-8 code page.
void test_file_name_not_ascii()
{
    TempDir    dir { "io" };
    auto const file = dir.path / fs::path { u8"Café ☕.json" };
    CHECK(write_file_atomically(file, "{}").has_value());
    CHECK(fs::exists(file));
    CHECK_EQ(read_file_by_name(file.string()).value_or(""), "{}");
    CHECK(file.string().ends_with("Café ☕.json"));
}

void test_home_dir()
{
    CHECK(fs::is_directory(home_dir()));
}

void test_user_dirs()
{
    auto const config = user_config_dir("SweatTrails");
    auto const data = user_data_dir("SweatTrails");
    auto const state = user_state_dir("SweatTrails");
    CHECK_EQ(config.filename().string(), "SweatTrails");
#if defined(_WIN32)
    // Settings roam with the user's profile; the store and the state don't.
    CHECK_EQ(config.parent_path().filename().string(), "Roaming");
    CHECK_EQ(data.parent_path().filename().string(), "Local");
    CHECK(data == state);
#elif defined(__APPLE__)
    auto const support = home_dir() / "Library" / "Application Support" / "SweatTrails";
    CHECK(config == support);
    CHECK(data == support);
    CHECK(state == support);
#else
    set_environment("XDG_CONFIG_HOME", "/xdg/config");
    set_environment("XDG_DATA_HOME", "relative/is/ignored");
    unset_environment("XDG_STATE_HOME");
    CHECK(user_config_dir("SweatTrails") == fs::path { "/xdg/config/SweatTrails" });
    CHECK(user_data_dir("SweatTrails") == home_dir() / ".local" / "share" / "SweatTrails");
    CHECK(user_state_dir("SweatTrails") == home_dir() / ".local" / "state" / "SweatTrails");
#endif
}

void test_executable_path()
{
    auto const exe = executable_path();
    CHECK(exe.is_absolute());
    CHECK(fs::is_regular_file(exe));
    CHECK_EQ(exe.stem().string(), "io_test");
}

// Where a packaged app keeps share/: in a macOS bundle's Resources, or
// next to sweattrails.exe on Windows. Otherwise, the fallback.
void test_find_data_dir()
{
    TempDir    dir { "io" };
    auto const fallback = dir.path / "build" / "share";

    auto const bundle = dir.path / "SweatTrails.app" / "Contents";
    fs::create_directories(bundle / "MacOS");
    auto const in_bundle = bundle / "MacOS" / "sweattrails";
    CHECK(find_data_dir(in_bundle, fallback) == fallback);
    fs::create_directories(bundle / "Resources" / "share");
    CHECK(fs::equivalent(find_data_dir(in_bundle, fallback), bundle / "Resources" / "share"));

    auto const install = dir.path / "Programs" / "SweatTrails";
    fs::create_directories(install);
    auto const installed = install / "sweattrails.exe";
    CHECK(find_data_dir(installed, fallback) == fallback);
    fs::create_directories(install / "share");
    CHECK(find_data_dir(installed, fallback) == install / "share");
}

// A bundle's Resources come before share next to the program.
void test_find_data_dir_prefers_the_bundle()
{
    TempDir    dir { "io" };
    auto const macos = dir.path / "Contents" / "MacOS";
    fs::create_directories(macos / "share");
    fs::create_directories(dir.path / "Contents" / "Resources" / "share");
    CHECK(fs::equivalent(find_data_dir(macos / "sweattrails", "fallback"), dir.path / "Contents" / "Resources" / "share"));
}

void test_find_data_dir_needs_a_directory()
{
    TempDir dir { "io" };
    CHECK(write_file_atomically(dir.path / "share", "not a directory").has_value());
    CHECK(find_data_dir(dir.path / "sweattrails.exe", "fallback") == fs::path { "fallback" });
}

}

int main()
{
    test_write_file_atomically();
    test_write_file_atomically_with_permissions();
    test_write_file_atomically_fails_cleanly();
    test_file_name_not_ascii();
    test_home_dir();
    test_user_dirs();
    test_executable_path();
    test_find_data_dir();
    test_find_data_dir_prefers_the_bundle();
    test_find_data_dir_needs_a_directory();
    return ST::Test::test_result();
}
