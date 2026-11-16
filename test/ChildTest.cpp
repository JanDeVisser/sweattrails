/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Starting programs, with test/FakeProgram.cpp as the program. That no
 * other files go to the program is in EncoderTest.
 */

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <Child.h>
#include <FileIO.h>

#include <TempDir.h>
#include <TestApp.h>

using namespace ST;
using namespace ST::Test;
namespace fs = std::filesystem;

namespace {

std::string contents(fs::path const &path)
{
    return read_file_by_name(path.string()).value_or(std::string { });
}

std::expected<int, LibCError> run(std::vector<std::string> const &args)
{
    std::vector<std::string> command { FAKE_PROGRAM };
    command.append_range(args);
    auto child = Child::start(command, Child::Options { });
    if (!child) {
        return std::unexpected(child.error());
    }
    return child->wait();
}

void test_exit_codes()
{
    CHECK_EQ(run({ }).value_or(-1), 0);
    CHECK_EQ(run({ "exit", "3" }).value_or(-1), 3);
    CHECK_EQ(run({ "sleep", "50", "exit", "4" }).value_or(-1), 4);
}

void test_missing_program()
{
    CHECK(!Child::start({ "sweattrails-no-such-program" }, Child::Options { }).has_value());
}

// The arguments arrive as they were, which on Windows takes quoting them
// into one command line.
void test_arguments()
{
    TempDir                        dir { "child-arguments" };
    auto const                     file = dir.path / "args";
    std::vector<std::string> const args {
        "plain",
        "two words",
        "",
        "a \"quoted\" word",
        "back\\slash",
        "trailing\\",
        "trailing space\\",
        "\\\\\"",
        "Café ☕",
    };
    std::vector<std::string> command { "args", file.string() };
    command.append_range(args);
    CHECK_EQ(run(command).value_or(-1), 0);
    std::string expected;
    for (auto const &arg : args) {
        expected += arg + "\n";
    }
    CHECK_EQ(contents(file), expected);
}

void test_stdin_and_stderr()
{
    TempDir    dir { "child-pipes" };
    auto const output = dir.path / "out";
    auto const log = dir.path / "log";
    auto       child = Child::start({ FAKE_PROGRAM, "stderr", "frame=1\r", "copy", output.string(), "stderr", "done\n" },
        Child::Options { .pipe_stdin = true, .discard_stdout = true, .stderr_file = log });
    CHECK(child.has_value());
    if (!child) {
        return;
    }
    std::string const bytes { "\x00\r\n\x1A\xFF", 5 };
    CHECK(child->write({ reinterpret_cast<unsigned char const *>(bytes.data()), bytes.size() }));
    CHECK_EQ(child->wait().value_or(-1), 0);
    CHECK(contents(output) == bytes);
    CHECK_EQ(contents(log), "frame=1\rdone\n");
}

// Writing to a program that has gone fails, and doesn't end the app.
void test_write_after_exit()
{
    auto child = Child::start({ FAKE_PROGRAM, "exit", "0" }, Child::Options { .pipe_stdin = true });
    CHECK(child.has_value());
    if (!child) {
        return;
    }
    std::vector<unsigned char> const frame(1 << 20);
    auto                             written = true;
    for (auto n = 0; n < 10 && written; ++n) {
        written = child->write(frame);
    }
    CHECK(!written);
    CHECK_EQ(child->wait().value_or(-1), 0);
}

void test_kill()
{
    auto child = Child::start({ FAKE_PROGRAM, "sleep", "30000" }, Child::Options { });
    CHECK(child.has_value());
    if (!child) {
        return;
    }
    auto const started = std::chrono::steady_clock::now();
    child->kill();
    auto const ended = child->wait();
    CHECK(!ended.has_value() || *ended != 0);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds { 5 });
    // Once it's been waited for, there's nothing left to kill.
    child->kill();
}

// kill() from another thread, while this one waits.
void test_kill_while_waiting()
{
    auto child = Child::start({ FAKE_PROGRAM, "sleep", "30000" }, Child::Options { });
    CHECK(child.has_value());
    if (!child) {
        return;
    }
    auto const  started = std::chrono::steady_clock::now();
    std::thread killer { [&child]() {
        std::this_thread::sleep_for(std::chrono::milliseconds { 300 });
        child->kill();
    } };
    auto const ended = child->wait();
    killer.join();
    CHECK(!ended.has_value() || *ended != 0);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds { 10 });
}

}

int main()
{
    test_exit_codes();
    test_missing_program();
    test_arguments();
    test_stdin_and_stderr();
    test_write_after_exit();
    test_kill();
    test_kill_while_waiting();
    return test_result();
}
