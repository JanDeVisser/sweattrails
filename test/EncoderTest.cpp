/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The encoder, with test/FakeProgram.cpp standing in for ffmpeg.
 */

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <FileIO.h>

#include <overlay/Encoder.h>

#include <TempDir.h>
#include <TestApp.h>

using namespace ST;
namespace fs = std::filesystem;

namespace {

// Frame `n`: four opaque pixels, numbered.
Frame numbered(Encoder &encoder, unsigned char n)
{
    auto ret = encoder.buffer();
    for (auto ix = 0uz; ix < ret.size(); ix += 4) {
        ret[ix] = n;
        ret[ix + 1] = static_cast<unsigned char>(ix);
        ret[ix + 2] = 0;
        ret[ix + 3] = 255;
    }
    return ret;
}

// A file opened the way that lets programs the app starts have it, unless
// the encoder stops them: without close-on-exec, or inheritable. Its file
// descriptor or handle, as a number.
long long open_inheritable(fs::path const &path)
{
#ifdef _WIN32
    SECURITY_ATTRIBUTES inherit { .nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE };
    auto const          handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(handle != INVALID_HANDLE_VALUE);
    return reinterpret_cast<long long>(handle);
#else
    auto const fd = ::open(path.c_str(), O_WRONLY | O_CREAT, 0644);
    CHECK(fd >= 0);
    return fd;
#endif
}

void close_inheritable(long long file)
{
#ifdef _WIN32
    CloseHandle(reinterpret_cast<HANDLE>(file));
#else
    ::close(static_cast<int>(file));
#endif
}

// The encoder, which the test can't do without.
std::unique_ptr<Encoder> started(std::vector<std::string> const &args, fs::path const &output, size_t frame_bytes)
{
    auto encoder = Encoder::start(FAKE_PROGRAM, args, output, frame_bytes);
    if (!encoder) {
        fatal("Can't start {}: {}", FAKE_PROGRAM, encoder.error());
    }
    return std::move(*encoder);
}

std::string contents(fs::path const &path)
{
    auto const text = read_file_by_name(path.string());
    return text ? *text : std::string { };
}

void test_last_line()
{
    CHECK_EQ(last_line(""), "");
    CHECK_EQ(last_line("one\ntwo\n"), "two");
    CHECK_EQ(last_line("frame=  10 fps=5\rframe=  20 fps=5\rError writing trailer: No space left on device\n\n"),
        "Error writing trailer: No space left on device");
    CHECK_EQ(last_line("only"), "only");
}

void test_frames_arrive()
{
    Test::TempDir dir { "encoder-frames" };
    auto const    output = dir.path / "out.raw";
    auto          encoder = started({ "copy", output.string() }, output, 16);
    for (unsigned char n = 0; n < 10; ++n) {
        CHECK(encoder->write(numbered(*encoder, n)));
    }
    CHECK(encoder->finish().has_value());
    auto const raw = contents(output);
    CHECK_EQ(raw.size(), 160uz);
    auto in_order = raw.size() == 160;
    for (auto ix = 0uz; in_order && ix < raw.size(); ix += 4) {
        in_order = static_cast<unsigned char>(raw[ix]) == ix / 16 && static_cast<unsigned char>(raw[ix + 1]) == ix % 16;
    }
    CHECK(in_order);
    CHECK(fs::exists(output));
}

void test_straight_alpha()
{
    Test::TempDir dir { "encoder-alpha" };
    auto const    output = dir.path / "out.raw";
    auto          encoder = started({ "copy", output.string() }, output, 4);
    auto          frame = encoder->buffer();
    frame = { 64, 32, 0, 128 };
    CHECK(encoder->write(std::move(frame)));
    CHECK(encoder->finish().has_value());
    auto const raw = contents(output);
    CHECK_EQ(raw.size(), 4uz);
    if (raw.size() == 4) {
        CHECK_EQ(static_cast<unsigned char>(raw[0]), 128);
        CHECK_EQ(static_cast<unsigned char>(raw[1]), 64);
    }
}

void test_failing()
{
    Test::TempDir dir { "encoder-failing" };
    auto const    output = dir.path / "out.mov";
    MUST(write_file_by_name(output.string(), std::string_view { "half a movie" }));
    auto encoder = started({ "stderr", "ffmpeg version 8\n", "stderr", "frame=1\r", "stderr", "No space left on device\n", "exit", "3" }, output, 1 << 20);
    // A frame larger than the pipe holds can't be written once it's gone.
    auto failed = false;
    for (auto n = 0; n < 10 && !failed; ++n) {
        failed = !encoder->write(encoder->buffer());
    }
    CHECK(failed);
    CHECK(!encoder->can_write());
    auto const finished = encoder->finish();
    CHECK(!finished.has_value());
    if (!finished) {
        CHECK_EQ(finished.error(), "No space left on device");
    }
    CHECK(!fs::exists(output));
}

void test_exit_code()
{
    Test::TempDir dir { "encoder-exit" };
    auto const    output = dir.path / "out.mov";
    auto          encoder = started({ "drain", "exit", "3" }, output, 4);
    CHECK(encoder->write(encoder->buffer()));
    auto const finished = encoder->finish();
    CHECK(!finished.has_value());
    if (!finished) {
        CHECK_EQ(finished.error(), "The encoder stopped with exit code 3");
    }
}

void test_stop()
{
    Test::TempDir dir { "encoder-stop" };
    auto const    output = dir.path / "out.raw";
    auto          encoder = started({ "copy", output.string() }, output, 1 << 16);
    CHECK(encoder->write(numbered(*encoder, 1)));
    CHECK(encoder->write(numbered(*encoder, 2)));
    encoder->stop();
    CHECK(!fs::exists(output));
    // Stopping twice, and the destructor after it, do nothing more.
    encoder->stop();
    auto const finished = encoder->finish();
    CHECK(!finished.has_value());
    if (!finished) {
        CHECK_EQ(finished.error(), "Stopped");
    }
}

void test_stop_by_destructor()
{
    Test::TempDir dir { "encoder-destructor" };
    auto const    output = dir.path / "out.raw";
    {
        auto encoder = started({ "copy", output.string() }, output, 16);
        CHECK(encoder->write(numbered(*encoder, 1)));
    }
    CHECK(!fs::exists(output));
}

// A file the app has open, without close-on-exec, isn't the program's.
void test_no_files_inherited()
{
    Test::TempDir dir { "encoder-inherit" };
    auto const    output = dir.path / "out.raw";
    auto const    file = open_inheritable(dir.path / "open");
    auto          encoder = started({ "inherited", std::to_string(file), "copy", output.string() }, output, 4);
    CHECK(encoder->write(encoder->buffer()));
    auto const finished = encoder->finish();
    CHECK(finished.has_value());
    if (!finished) {
        CHECK_EQ(finished.error(), "<none>");
    }
    close_inheritable(file);
}

// The program finishes on the encoder's thread: close() doesn't wait.
void test_close_doesnt_wait()
{
    Test::TempDir dir { "encoder-close" };
    auto const    output = dir.path / "out.raw";
    auto          encoder = started({ "drain", "sleep", "1000", "write", output.string(), "done" }, output, 4);
    CHECK(encoder->write(encoder->buffer()));
    auto const before = std::chrono::steady_clock::now();
    encoder->close();
    CHECK(!encoder->result().has_value());
    CHECK(std::chrono::steady_clock::now() - before < std::chrono::milliseconds { 500 });
    while (!encoder->result()) {
        std::this_thread::sleep_for(std::chrono::milliseconds { 10 });
    }
    CHECK(encoder->result()->has_value());
    CHECK(encoder->finish().has_value());
    CHECK_EQ(contents(output), "done\n");
}

void test_no_program()
{
    auto const encoder = Encoder::start("/nowhere/ffmpeg", { }, "/tmp/never.mov", 4);
    CHECK(!encoder.has_value());
    if (!encoder) {
        CHECK(encoder.error().starts_with("Can't start ffmpeg"));
    }
}

// The file descriptors, or handles on Windows, the test has open.
int open_fds()
{
#ifdef _WIN32
    DWORD count { 0 };
    GetProcessHandleCount(GetCurrentProcess(), &count);
    return static_cast<int>(count);
#else
    auto ret { 0 };
    for (auto fd = 0; fd < 1024; ++fd) {
        if (::fcntl(fd, F_GETFD) != -1) {
            ++ret;
        }
    }
    return ret;
#endif
}

// Whether the encoder left a child behind, as a zombie or still running.
// Windows has no zombies, and a handle to a child is in open_fds().
bool has_children()
{
#ifdef _WIN32
    return false;
#else
    return !(::waitpid(-1, nullptr, WNOHANG) == -1 && errno == ECHILD);
#endif
}

// Finishing, failing and stopping close the pipe, and wait for the
// program. Reviewer's test.
void test_no_leaks()
{
    Test::TempDir dir { "encoder-leaks" };
    auto const    fds = open_fds();
    {
        auto encoder = started({ "copy", (dir.path / "a.raw").string() }, dir.path / "a.raw", 16);
        CHECK(encoder->write(numbered(*encoder, 1)));
        CHECK(encoder->finish().has_value());
    }
    CHECK_EQ(open_fds(), fds);
    CHECK(!has_children());
    {
        auto encoder = started({ "exit", "3" }, dir.path / "b.mov", 1 << 20);
        for (auto n = 0; n < 10 && encoder->write(encoder->buffer()); ++n) { }
        CHECK(!encoder->finish().has_value());
    }
    CHECK_EQ(open_fds(), fds);
    CHECK(!has_children());
    {
        auto encoder = started({ "copy", (dir.path / "c.raw").string() }, dir.path / "c.raw", 16);
        CHECK(encoder->write(numbered(*encoder, 1)));
        encoder->stop();
    }
    CHECK_EQ(open_fds(), fds);
    CHECK(!has_children());
}

// A program that doesn't read: the writer is stuck in the middle of a
// frame, and stop() still returns. Reviewer's test.
void test_stop_while_stuck()
{
    Test::TempDir dir { "encoder-stuck" };
    auto const    output = dir.path / "out.mov";
    auto          encoder = started({ "sleep", "30000" }, output, 1 << 20);
    CHECK(encoder->write(encoder->buffer()));
    CHECK(encoder->write(encoder->buffer()));
    CHECK(!encoder->can_write());
    auto const started_at = std::chrono::steady_clock::now();
    encoder->stop();
    CHECK(std::chrono::steady_clock::now() - started_at < std::chrono::seconds { 5 });
    CHECK(!has_children());
}

// A program that exits without an error, before it has all the frames:
// that's a failure too, and the file goes. Reviewer's test.
void test_early_exit_without_an_error()
{
    Test::TempDir dir { "encoder-early" };
    auto const    output = dir.path / "out.mov";
    MUST(write_file_by_name(output.string(), std::string_view { "half a movie" }));
    auto encoder = started({ "exit", "0" }, output, 1 << 20);
    auto failed = false;
    for (auto n = 0; n < 10 && !failed; ++n) {
        failed = !encoder->write(encoder->buffer());
    }
    CHECK(failed);
    CHECK(encoder->failed());
    CHECK(!encoder->finish().has_value());
    CHECK(!fs::exists(output));
}

}

int main()
{
    test_last_line();
    test_frames_arrive();
    test_straight_alpha();
    test_failing();
    test_exit_code();
    test_stop();
    test_stop_by_destructor();
    test_no_files_inherited();
    test_close_doesnt_wait();
    test_no_program();
    test_no_leaks();
    test_stop_while_stuck();
    test_early_exit_without_an_error();
    return ST::Test::test_result();
}
