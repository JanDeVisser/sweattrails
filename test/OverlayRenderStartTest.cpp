/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Starting an overlay render that can't start: no ffmpeg, or a font that
 * isn't there. Neither gets as far as raylib, so this runs without a window.
 */

#include <filesystem>
#include <memory>
#include <optional>
#include <print>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

#include <overlay/LayoutFile.h>
#include <overlay/OverlayRender.h>
#include <storage/Storage.h>
#include <widget/DisplayedActivity.h>

#include <TempDir.h>
#include <TestApp.h>

using namespace ST;
namespace fs = std::filesystem;

namespace {

// Runs `test` in a child process, so that a crash or a hang is a failed
// check here rather than the end of the test.
template<typename Test>
void without_crashing(char const *name, Test const &test)
{
    auto const pid = ::fork();
    if (pid == 0) {
        // A hang is a failure too.
        ::alarm(3);
        test();
        ::_exit(ST::Test::test_result());
    }
    int status { 0 };
    ::waitpid(pid, &status, 0);
    if (WIFSIGNALED(status)) {
        std::println(stderr, "{}: crashed or hung, with signal {}", name, WTERMSIG(status));
    }
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

RenderJob job(fs::path const &output, fs::path const &ffmpeg)
{
    static Storage storage { };
    Activity       activity { storage, ActivityID { } };
    activity.segment.start_time.timestamp = 1'750'000'000;
    activity.records.push_back(record { .timestamp = DateTime { .timestamp = 1'750'000'000 } });
    activity.records.push_back(record { .timestamp = DateTime { .timestamp = 1'750'000'060 } });
    LayoutFile file { fs::path { SWEATTRAILS_SOURCE_DIR } / "share" / "overlays" / "cycling-basic.overlay.json" };
    file.reload_if_changed();
    return RenderJob {
        .activity = std::make_shared<DisplayedActivity>(activity),
        .layout = file.layout ? *file.layout : OverlayLayout { },
        .layout_file = "cycling-basic.overlay.json",
        .settings = RenderSettings { .folder = output.parent_path() },
        .range = RenderRange { .from = 0, .to = 10 },
        .output = output,
        .ffmpeg = ffmpeg,
    };
}

// The layout has no tiles, and nothing is downloaded.
std::expected<std::string, LibCError> no_tiles(Tile const &)
{
    return std::unexpected(LibCError { ENOENT });
}

void no_downloads(pTileDownload const &)
{
}

void test_without_ffmpeg()
{
    without_crashing("test_without_ffmpeg", []() {
        Test::TempDir dir { "render-start-ffmpeg" };
        auto const    found = [](std::string_view) { return std::optional<fs::path> { "/nowhere/font.ttf" }; };
        auto const    render = OverlayRender::start(job(dir.path / "out.mov", "/nowhere/ffmpeg"), found, ImageFinder { }, no_tiles, no_downloads);
        CHECK(!render.has_value());
        if (!render) {
            CHECK(render.error().starts_with("Can't start ffmpeg"));
        }
    });
}

void test_without_a_font()
{
    without_crashing("test_without_a_font", []() {
        Test::TempDir dir { "render-start-font" };
        auto const    missing = [](std::string_view) { return std::optional<fs::path> { }; };
        auto const    render = OverlayRender::start(job(dir.path / "out.mov", "/bin/sh"), missing, ImageFinder { }, no_tiles, no_downloads);
        CHECK(!render.has_value());
        if (!render) {
            CHECK(render.error().starts_with("cycling-basic.overlay.json: "));
        }
    });
}

}

int main()
{
    test_without_ffmpeg();
    test_without_a_font();
    return ST::Test::test_result();
}
