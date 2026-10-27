/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The encoder, with /bin/sh standing in for ffmpeg.
 */

#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include <IO.h>

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

// The encoder, which the test can't do without.
std::unique_ptr<Encoder> started(std::vector<std::string> const &args, fs::path const &output, size_t frame_bytes)
{
    auto encoder = Encoder::start("/bin/sh", args, output, frame_bytes);
    if (!encoder) {
        fatal("Can't start /bin/sh: {}", encoder.error());
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
    auto          encoder = started({ "-c", "exec cat > \"$1\"", "sh", output.string() }, output, 16);
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
    auto          encoder = started({ "-c", "exec cat > \"$1\"", "sh", output.string() }, output, 4);
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
    auto const script = "echo 'ffmpeg version 8' >&2; printf 'frame=1\\r' >&2; echo 'No space left on device' >&2; exit 3";
    auto       encoder = started({ "-c", script }, output, 1 << 20);
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
    auto          encoder = started({ "-c", "cat > /dev/null; exit 3" }, output, 4);
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
    auto          encoder = started({ "-c", "exec cat > \"$1\"", "sh", output.string() }, output, 1 << 16);
    CHECK(encoder->write(numbered(*encoder, 1)));
    CHECK(encoder->write(numbered(*encoder, 2)));
    encoder->stop();
    CHECK(!fs::exists(output));
    // Stopping twice, and the destructor after it, do nothing more.
    encoder->stop();
    CHECK(encoder->finish().has_value());
}

void test_stop_by_destructor()
{
    Test::TempDir dir { "encoder-destructor" };
    auto const    output = dir.path / "out.raw";
    {
        auto encoder = started({ "-c", "exec cat > \"$1\"", "sh", output.string() }, output, 16);
        CHECK(encoder->write(numbered(*encoder, 1)));
    }
    CHECK(!fs::exists(output));
}

void test_no_program()
{
    auto const encoder = Encoder::start("/nowhere/ffmpeg", { }, "/tmp/never.mov", 4);
    CHECK(!encoder.has_value());
    if (!encoder) {
        CHECK(encoder.error().starts_with("Can't start ffmpeg"));
    }
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
    test_no_program();
    return ST::Test::test_result();
}
