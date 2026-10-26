/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Loading overlay layout files, and again when they change; the list of
 * layouts; and the layout the user picked.
 */

#include <chrono>
#include <filesystem>
#include <string>
#include <variant>

#include <IO.h>

#include <overlay/LayoutFile.h>

#include <TempDir.h>
#include <TestApp.h>

using namespace ST;
namespace fs = std::filesystem;

namespace {

constexpr char const OneText[] = R"({ "elements": [ { "type": "text", "text": "a" } ] })";

// Writes the file, and moves its time on, as an editor saving it later
// would: two writes in a test can fall in the same tick of the clock.
void write(fs::path const &path, std::string_view contents)
{
    std::error_code ec { };
    auto const      before = fs::last_write_time(path, ec);
    MUST(write_file_by_name(path.string(), contents));
    if (!ec) {
        fs::last_write_time(path, before + std::chrono::seconds { 1 });
    }
}

void put(fs::path const &path, std::string_view contents)
{
    MUST(write_file_by_name(path.string(), contents));
}

std::string error_of(LayoutFile const &file)
{
    return file.layout ? std::string { "ok" } : file.layout.error();
}

void test_load_and_reload()
{
    Test::TempDir dir { "layout-file" };
    auto const    path = dir.path / "mine.overlay.json";
    write(path, OneText);
    LayoutFile file { path };
    CHECK(file.reload_if_changed());
    CHECK_EQ(error_of(file), "ok");
    CHECK_EQ(file.layout ? file.layout->elements.size() : 0uz, 1uz);
    // Unchanged: not loaded again.
    CHECK(!file.reload_if_changed());
    // Changed: loaded again.
    write(path, R"({ "elements": [ { "type": "text", "text": "a" }, { "type": "text", "text": "b" } ] })");
    CHECK(file.reload_if_changed());
    CHECK_EQ(file.layout ? file.layout->elements.size() : 0uz, 2uz);
}

// An error is shown until the file is fixed.
void test_errors_and_recovering()
{
    Test::TempDir dir { "layout-file-errors" };
    auto const    path = dir.path / "mine.overlay.json";
    write(path, R"({ "elements": [ { "type": "txt" } ] })");
    LayoutFile file { path };
    CHECK(file.reload_if_changed());
    CHECK_EQ(error_of(file), "mine.overlay.json: element 1: \"type\": `txt` isn't an element type");
    write(path, "{ \"elements\": [ ");
    CHECK(file.reload_if_changed());
    CHECK(error_of(file).starts_with("mine.overlay.json: "));
    CHECK(error_of(file).contains("JSON error"));
    write(path, OneText);
    CHECK(file.reload_if_changed());
    CHECK_EQ(error_of(file), "ok");
    // Removed: an error, and back when it's back.
    fs::remove(path);
    CHECK(file.reload_if_changed());
    CHECK(error_of(file).starts_with("mine.overlay.json: "));
    CHECK(!file.reload_if_changed());
    write(path, OneText);
    CHECK(file.reload_if_changed());
    CHECK_EQ(error_of(file), "ok");
}

void test_overlay_layouts()
{
    Test::TempDir builtin { "layouts-builtin" };
    Test::TempDir user { "layouts-user" };
    put(builtin.path / "cycling-basic.overlay.json", OneText);
    put(builtin.path / "running.overlay.json", OneText);
    put(builtin.path / "notes.json", "{}");
    put(builtin.path / ".overlay.json", "{}");
    put(user.path / "cycling-basic.overlay.json", OneText);
    put(user.path / "alps.overlay.json", OneText);
    auto const layouts = overlay_layouts(builtin.path, user.path);
    CHECK_EQ(layouts.size(), 3uz);
    if (layouts.size() == 3) {
        CHECK_EQ(layouts[0].name, "alps");
        CHECK_EQ(layouts[1].name, "cycling-basic");
        CHECK(layouts[1].path == user.path / "cycling-basic.overlay.json");
        CHECK_EQ(layouts[2].name, "running");
        CHECK(layouts[2].path == builtin.path / "running.overlay.json");
    }
    // A user directory that isn't there.
    CHECK_EQ(overlay_layouts(builtin.path, user.path / "nope").size(), 2uz);
}

void test_chosen_layout()
{
    Test::TempDir dir { "chosen-layout" };
    auto const    state = dir.path / "state" / "overlay.json";
    CHECK(!read_chosen_layout(state).has_value());
    CHECK(save_chosen_layout(state, "alps").has_value());
    CHECK_EQ(read_chosen_layout(state).value_or(""), "alps");
    CHECK(save_chosen_layout(state, "cycling-basic").has_value());
    CHECK_EQ(read_chosen_layout(state).value_or(""), "cycling-basic");
    put(state, "not json");
    CHECK(!read_chosen_layout(state).has_value());
}

// The layout the preview shows first loads, and only uses fonts that are
// shipped.
void test_builtin_layout()
{
    LayoutFile file { fs::path { SWEATTRAILS_SOURCE_DIR } / "share" / "overlays" / "cycling-basic.overlay.json" };
    CHECK(file.reload_if_changed());
    CHECK_EQ(error_of(file), "ok");
    if (!file.layout) {
        return;
    }
    CHECK_EQ(file.layout->elements.size(), 10uz);
    CHECK_EQ(file.layout->skipped, 0uz);
    for (auto const &element : file.layout->elements) {
        std::visit(
            [](auto const &e) {
                CHECK(fs::exists(fs::path { SWEATTRAILS_SOURCE_DIR } / "share" / "fonts" / e.style.font));
            },
            element);
    }
}

}

int main()
{
    test_load_and_reload();
    test_errors_and_recovering();
    test_overlay_layouts();
    test_chosen_layout();
    test_builtin_layout();
    return ST::Test::test_result();
}
