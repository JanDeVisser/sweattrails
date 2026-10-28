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
#include <format>
#include <string>
#include <vector>

#include <IO.h>
#include <JSON.h>

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

// The built-in layouts load, and only use fonts that are shipped, also
// for their units.
void test_builtin_layout(std::string_view name, size_t elements)
{
    LayoutFile file { fs::path { SWEATTRAILS_SOURCE_DIR } / "share" / "overlays" / std::format("{}.overlay.json", name) };
    CHECK(file.reload_if_changed());
    CHECK_EQ(error_of(file), "ok");
    if (!file.layout) {
        return;
    }
    CHECK_EQ(file.layout->elements.size(), elements);
    CHECK_EQ(file.layout->skipped, 0uz);
    auto const shipped = [](std::string const &font) {
        return fs::exists(fs::path { SWEATTRAILS_SOURCE_DIR } / "share" / "fonts" / font);
    };
    for (auto const &element : file.layout->elements) {
        for (auto const &font : element_fonts(element)) {
            CHECK(shipped(font));
        }
    }
}

// A file that isn't there yet: an error, once, until it's made.
void test_missing_file()
{
    Test::TempDir dir { "layout-file-missing" };
    auto const    path = dir.path / "later.overlay.json";
    LayoutFile    file { path };
    CHECK(file.reload_if_changed());
    CHECK(error_of(file).starts_with("later.overlay.json: "));
    CHECK(!file.reload_if_changed());
    put(path, OneText);
    CHECK(file.reload_if_changed());
    CHECK_EQ(error_of(file), "ok");
}

// The error names the file, the element and the property, as in the spec.
void test_error_names_file_element_and_property()
{
    Test::TempDir dir { "layout-file-message" };
    auto const    path = dir.path / "cycling-basic.overlay.json";
    put(path, R"({ "elements": [
        // A comment, as layouts have them.
        { "type": "text", "text": "a" },
        { "type": "image" },
        { "type": "value", "field": "speed", "colour": "#ffffff" }
    ] })");
    LayoutFile file { path };
    CHECK(file.reload_if_changed());
    CHECK_EQ(error_of(file), "cycling-basic.overlay.json: element 3: \"colour\" isn't a property of a value");
}

void test_no_layout_directories()
{
    Test::TempDir dir { "layouts-none" };
    CHECK(overlay_layouts(dir.path / "a", dir.path / "b").empty());
    // A directory without layout files has none.
    CHECK(overlay_layouts(dir.path, dir.path / "b").empty());
}

void test_chosen_layout_not_a_name()
{
    Test::TempDir dir { "chosen-layout-bad" };
    auto const    state = dir.path / "overlay.json";
    put(state, R"({ "layout": "" })");
    CHECK(!read_chosen_layout(state).has_value());
    put(state, R"({ "other": "alps" })");
    CHECK(!read_chosen_layout(state).has_value());
    put(state, "[]");
    CHECK(!read_chosen_layout(state).has_value());
}

OverlayLayout decoded(std::string_view json)
{
    return MUST_EVAL(decode_layout(MUST_EVAL(JSONValue::deserialize(json))));
}

// The fonts are looked up once each, and a missing one names the element
// that uses it, counting the elements of later slices that were skipped.
void test_layout_fonts()
{
    auto const               layout = decoded(R"({ "elements": [
        { "type": "image" },
        { "type": "text", "text": "a", "font": "A.ttf" },
        { "type": "value", "field": "speed", "font": "A.ttf", "unit_style": { "font": "B.ttf" } }
    ] })");
    std::vector<std::string> asked;
    auto const               find_all = [&asked](std::string_view name) -> std::optional<fs::path> {
        asked.emplace_back(name);
        return fs::path { "/fonts" } / name;
    };
    auto const fonts = layout_fonts(layout, find_all);
    CHECK(fonts.has_value());
    if (fonts) {
        CHECK_EQ(fonts->size(), 2uz);
        CHECK(fonts->at("B.ttf") == fs::path { "/fonts/B.ttf" });
    }
    CHECK_EQ(asked.size(), 2uz);

    auto const only_a = [](std::string_view name) -> std::optional<fs::path> {
        if (name == "A.ttf") {
            return fs::path { "/fonts/A.ttf" };
        }
        return { };
    };
    auto const missing = layout_fonts(layout, only_a);
    CHECK(!missing.has_value());
    if (!missing) {
        CHECK_EQ(missing.error().to_string(), "element 3: the font `B.ttf` isn't in any of the font directories");
    }
    auto const none = layout_fonts(layout, [](std::string_view) -> std::optional<fs::path> { return { }; });
    CHECK(!none.has_value());
    if (!none) {
        CHECK_EQ(none.error().to_string(), "element 2: the font `A.ttf` isn't in any of the font directories");
    }
}
}

int main()
{
    test_load_and_reload();
    test_errors_and_recovering();
    test_overlay_layouts();
    test_chosen_layout();
    test_builtin_layout("cycling-basic", 11);
    test_builtin_layout("running-basic", 9);
    test_builtin_layout("cycling-dashboard", 17);
    test_layout_fonts();
    test_missing_file();
    test_error_names_file_element_and_property();
    test_no_layout_directories();
    test_chosen_layout_not_a_name();
    return ST::Test::test_result();
}
