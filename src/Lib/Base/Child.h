/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * A program the app runs, such as ffmpeg. ChildPosix.cpp and ChildWin32.cpp
 * start it.
 */

#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Error.h>

namespace ST {

// The app's own process ID.
int process_id();

// A program the app runs. It gets none of the app's open files, other than
// the ones asked for in its Options.
struct Child {
    struct Options {
        // A pipe to the program's stdin, for write(). Without it, the
        // program has the app's stdin.
        bool pipe_stdin { false };
        // Throws away what the program writes to stdout.
        bool discard_stdout { false };
        // The file the program's stderr goes to, instead of the app's.
        std::optional<std::filesystem::path> stderr_file { };
    };

    // Starts `command`: the program, looked for on the PATH, and its
    // arguments.
    static std::expected<Child, LibCError> start(std::vector<std::string> const &command, Options const &options);

    Child(Child &&) noexcept;
    Child &operator=(Child &&) noexcept;
    ~Child(); // Doesn't wait for the program, or kill it.

    // Writes all of `bytes` to the program's stdin; false once the program
    // has gone.
    bool write(std::span<unsigned char const> bytes);
    void close_stdin();
    // Waits for the program to end, and returns its exit code. A program
    // killed by a signal is an error.
    std::expected<int, LibCError> wait();
    // Kills the program, unless wait() is done with it. Another thread may
    // be waiting for it meanwhile.
    void kill();

    struct State;

private:
    explicit Child(std::unique_ptr<State> state);

    std::unique_ptr<State> m_state;
};

}
