/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * A program for tests to start, such as in place of ffmpeg, on any
 * platform. Its arguments are steps, done in order:
 *
 *   copy <file>         copies stdin to <file>, until stdin is closed
 *   drain               reads stdin until it's closed
 *   stderr <text>       writes <text> to stderr, as it is
 *   write <file> <text> writes <text> and a newline to <file>
 *   args <file>         writes the arguments after it to <file>, one per
 *                       line, and ends
 *   sleep <ms>          sleeps
 *   inherited <handle>  fails if it has the handle or file descriptor
 *   exit <code>         ends with <code>
 *
 * Without exit, it ends with 0.
 */

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#endif

namespace {

bool has_handle(std::string_view handle)
{
    auto const value = std::stoll(std::string { handle });
#ifdef _WIN32
    DWORD flags { 0 };
    return GetHandleInformation(reinterpret_cast<HANDLE>(value), &flags) != 0;
#else
    return ::fcntl(static_cast<int>(value), F_GETFD) != -1;
#endif
}

}

int main(int argc, char **argv)
{
#ifdef _WIN32
    // Bytes in, bytes out: no CRLF translation.
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    std::vector<std::string_view> args { argv + 1, argv + argc };
    for (size_t ix = 0; ix < args.size(); ++ix) {
        auto const step = args[ix];
        auto const next = [&]() { return (ix + 1 < args.size()) ? args[++ix] : std::string_view { }; };
        if (step == "copy") {
            std::ofstream out { std::string { next() }, std::ios::binary };
            out << std::cin.rdbuf();
        } else if (step == "drain") {
            for (char ch; std::cin.get(ch);) { }
        } else if (step == "stderr") {
            std::cerr << next() << std::flush;
        } else if (step == "write") {
            std::ofstream out { std::string { next() }, std::ios::binary };
            out << next() << '\n';
        } else if (step == "args") {
            std::ofstream out { std::string { next() }, std::ios::binary };
            for (++ix; ix < args.size(); ++ix) {
                out << args[ix] << '\n';
            }
        } else if (step == "sleep") {
            std::this_thread::sleep_for(std::chrono::milliseconds { std::stoi(std::string { next() }) });
        } else if (step == "inherited") {
            if (has_handle(next())) {
                std::cerr << "inherited\n";
                return 1;
            }
        } else if (step == "exit") {
            return std::stoi(std::string { next() });
        } else {
            std::cerr << "fake_program: unknown step " << step << '\n';
            return 2;
        }
    }
    return 0;
}
