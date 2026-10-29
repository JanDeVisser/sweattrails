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
    auto const shipped = [](std::string const &font) {
        return fs::exists(fs::path { SWEATTRAILS_SOURCE_DIR } / "share" / "fonts" / font);
    };
    for (auto const *element : all_elements(file.layout->elements)) {
        for (auto const &font : element_fonts(*element)) {
            CHECK(shipped(font));
        }
    }
    // And its pictures, as the app finds them.
    auto const builtin = fs::path { SWEATTRAILS_SOURCE_DIR } / "share" / "overlays";
    auto const images = layout_images(*file.layout, builtin, [&builtin](std::string_view src, fs::path const &dir) {
        return find_image(src, dir, builtin, [](fs::path const &p) { return fs::is_regular_file(p); });
    });
    CHECK_EQ(images ? std::string { "ok" } : images.error().to_string(), "ok");
}

// An icon before each of the dashboard's labels, tinted.
void test_dashboard_icons()
{
    LayoutFile file { fs::path { SWEATTRAILS_SOURCE_DIR } / "share" / "overlays" / "cycling-dashboard.overlay.json" };
    CHECK(file.reload_if_changed());
    if (!file.layout) {
        return;
    }
    std::vector<std::string> icons;
    for (auto const *element : all_elements(file.layout->elements)) {
        if (auto const *image = std::get_if<ImageElement>(element); image) {
            icons.push_back(std::format("{} {} #{:02x}{:02x}{:02x}{:02x}", image->src, image->width.value_or(0),
                image->tint.r, image->tint.g, image->tint.b, image->tint.a));
        }
    }
    CHECK_EQ(icons, (std::vector<std::string> { "icons/speedometer.png 24 #ffffffcc", "icons/heart.png 24 #f94144ff", "icons/cadence.png 24 #ffffffcc", "icons/bolt.png 24 #f9c74fff" }));
}

// The dashboard's map, as the spec's Version 1 scope has it: a circle,
// 300 by 300, following at zoom 15, the done track in the profile's amber,
// the remaining one faint white with an outline, and a white marker
// ringed in amber.
void test_dashboard_map()
{
    LayoutFile file { fs::path { SWEATTRAILS_SOURCE_DIR } / "share" / "overlays" / "cycling-dashboard.overlay.json" };
    CHECK(file.reload_if_changed());
    if (!file.layout) {
        return;
    }
    std::vector<MapElement> maps;
    Color                   profile_amber { };
    for (auto const &element : file.layout->elements) {
        if (auto const *map = std::get_if<MapElement>(&element); map) {
            maps.push_back(*map);
        }
        if (auto const *profile = std::get_if<ProfileElement>(&element); profile) {
            profile_amber = profile->done.line.color;
        }
    }
    CHECK_EQ(maps.size(), 1uz);
    if (maps.size() != 1) {
        return;
    }
    auto const &map = maps[0];
    auto const  same = [](Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; };
    CHECK(map.clip == ClipKind::Circle);
    CHECK(map.mode == MapMode::Follow);
    CHECK_EQ(map.zoom, 15);
    CHECK_EQ(map.at.size ? map.at.size->x : 0.0f, 300.0f);
    CHECK_EQ(map.at.size ? map.at.size->y : 0.0f, 300.0f);
    CHECK(map.panel.background.has_value());
    CHECK(map.panel.border.has_value());
    CHECK(same(map.track.done.color, profile_amber));
    CHECK(map.track.outline.has_value());
    CHECK(map.marker.has_value());
    if (map.marker) {
        CHECK(same(map.marker->color, WHITE));
        CHECK(same(map.marker->outline.value_or(BLANK), profile_amber));
    }
    // Dark OpenStreetMap, behind the footage, with its credit.
    CHECK(map.tiles.has_value());
    if (map.tiles) {
        CHECK_EQ(map.tiles->opacity, 0.6f);
        CHECK(map.tiles->filter.kind == TileFilterKind::Dark);
    }
    CHECK_EQ(map.attribution.font_size, 14.0f);
}

// The ride's name and date for the video's first 5 seconds, in a group
// with a panel, fading over half a second; everything else always shows.
void test_dashboard_title_card()
{
    LayoutFile file { fs::path { SWEATTRAILS_SOURCE_DIR } / "share" / "overlays" / "cycling-dashboard.overlay.json" };
    CHECK(file.reload_if_changed());
    if (!file.layout) {
        return;
    }
    std::vector<std::string> card;
    for (auto const *element : all_elements(file.layout->elements)) {
        auto const visible = std::visit([](auto const &e) { return e.visible; }, *element);
        if (!visible) {
            continue;
        }
        CHECK_EQ(visible->from, 0.0);
        CHECK_EQ(visible->to.value_or(0), 5.0);
        CHECK_EQ(visible->fade, 0.5);
        auto const *group = std::get_if<GroupElement>(element);
        if (group == nullptr) {
            card.push_back("not a group");
            continue;
        }
        card.push_back(group->panel.background ? "panel" : "no panel");
        for (auto const &child : group->children) {
            std::string shown;
            for (auto const &part : std::get<TextElement>(child).text.parts) {
                if (auto const *field = std::get_if<Placeholder>(&part)) {
                    shown += std::format("{{{}}}", OverlayField_name(field->field));
                }
            }
            card.push_back(shown);
        }
    }
    CHECK_EQ(card, (std::vector<std::string> { "panel", "{title}", "{date}" }));
}

// The dashboard's three panels are groups, each with what's in it, and no
// empty texts draw panels any more.
void test_dashboard_groups()
{
    LayoutFile file { fs::path { SWEATTRAILS_SOURCE_DIR } / "share" / "overlays" / "cycling-dashboard.overlay.json" };
    CHECK(file.reload_if_changed());
    if (!file.layout) {
        return;
    }
    std::vector<std::string> groups;
    for (auto const *element : all_elements(file.layout->elements)) {
        if (auto const *group = std::get_if<GroupElement>(element)) {
            groups.push_back(std::format("{} {}", group->number, group->children.size()));
        }
        if (auto const *text = std::get_if<TextElement>(element)) {
            CHECK(!text->text.parts.empty());
        }
    }
    CHECK_EQ(groups, (std::vector<std::string> { "1 3", "5 9", "10 2" }));
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
        { "type": "group", "size": { "width": 100, "height": 100 }, "children": [] },
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

// The fonts are looked up once each, also those of a group's children,
// and a missing one names the element that uses it.
void test_layout_fonts()
{
    auto const               layout = decoded(R"({ "elements": [
        { "type": "text", "text": "a", "font": "A.ttf" },
        { "type": "group", "size": { "width": 100, "height": 100 }, "children": [
            { "type": "value", "field": "speed", "font": "A.ttf", "unit_style": { "font": "B.ttf" } } ] }
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
        CHECK_EQ(missing.error().to_string(), "element 2.1: the font `B.ttf` isn't in any of the font directories");
    }
    auto const none = layout_fonts(layout, [](std::string_view) -> std::optional<fs::path> { return { }; });
    CHECK(!none.has_value());
    if (!none) {
        CHECK_EQ(none.error().to_string(), "element 1: the font `A.ttf` isn't in any of the font directories");
    }
}
}

// A picture next to the layout comes first, then a built-in one; an
// absolute path is what it says.
void test_find_image()
{
    std::set<fs::path> const files { "/mine/logo.png", "/builtin/icons/heart.png", "/builtin/logo.png", "/elsewhere/me.jpg" };
    auto const               exists = [&files](fs::path const &p) { return files.contains(p); };
    auto const               find = [&exists](std::string_view src) {
        auto const found = find_image(src, "/mine", "/builtin", exists);
        return found ? found->string() : std::string { "none" };
    };
    CHECK_EQ(find("logo.png"), "/mine/logo.png");
    CHECK_EQ(find("icons/heart.png"), "/builtin/icons/heart.png");
    CHECK_EQ(find("/elsewhere/me.jpg"), "/elsewhere/me.jpg");
    CHECK_EQ(find("/mine/me.jpg"), "none");
    CHECK_EQ(find("icons/bolt.png"), "none");
}

// Each picture is looked for once, and a missing one names the element
// that shows it.
void test_layout_images()
{
    auto const               layout = decoded(R"({ "elements": [
        { "type": "text", "text": "a" },
        { "type": "image", "src": "icons/heart.png" },
        { "type": "image", "src": "icons/heart.png" },
        { "type": "image", "src": "logo.png" }
    ] })");
    std::vector<std::string> asked;
    auto const               find = [&asked](std::string_view src, fs::path const &dir) -> std::optional<fs::path> {
        asked.push_back(std::format("{} in {}", src, dir.string()));
        return (src == "logo.png") ? std::nullopt : std::optional { dir / src };
    };
    auto const images = layout_images(layout, "/layouts", find);
    CHECK(!images.has_value());
    if (!images) {
        CHECK_EQ(images.error().to_string(),
            "element 4: the picture `logo.png` isn't there: it's looked for next to the layout, and with the built-in layouts");
    }
    CHECK_EQ(asked, (std::vector<std::string> { "icons/heart.png in /layouts", "logo.png in /layouts" }));

    auto const found = layout_images(decoded(R"({ "elements": [ { "type": "image", "src": "a.png" } ] })"), "/layouts", find);
    CHECK(found && found->at("a.png") == fs::path { "/layouts/a.png" });
}

// A picture only a group's child shows is looked for, and one that isn't
// there is named by the child's number.
void test_layout_images_in_groups()
{
    auto const               layout = decoded(R"({ "elements": [
        { "type": "text", "text": "a" },
        { "type": "group", "size": { "width": 100, "height": 100 }, "children": [
            { "type": "image", "src": "icons/heart.png" },
            { "type": "group", "size": { "width": 10, "height": 10 }, "children": [ { "type": "image", "src": "logo.png" } ] } ] }
    ] })");
    std::vector<std::string> asked;
    auto const               find = [&asked](std::string_view src, fs::path const &dir) -> std::optional<fs::path> {
        asked.push_back(std::string { src });
        return (src == "logo.png") ? std::nullopt : std::optional { dir / src };
    };
    auto const images = layout_images(layout, "/layouts", find);
    CHECK(!images.has_value());
    if (!images) {
        CHECK_EQ(images.error().to_string(),
            "element 2.2.1: the picture `logo.png` isn't there: it's looked for next to the layout, and with the built-in layouts");
    }
    CHECK_EQ(asked, (std::vector<std::string> { "icons/heart.png", "logo.png" }));
}

// A path up from the layout's folder is next to the layout too; an
// absolute one isn't looked for anywhere else.
void test_find_image_paths()
{
    std::set<fs::path> const files { "/mine/../shared/logo.png", "/builtin/logo.png" };
    auto const               exists = [&files](fs::path const &p) { return files.contains(p); };
    auto const               find = [&exists](std::string_view src) {
        auto const found = find_image(src, "/mine", "/builtin", exists);
        return found ? found->string() : std::string { "none" };
    };
    CHECK_EQ(find("../shared/logo.png"), "/mine/../shared/logo.png");
    CHECK_EQ(find("/logo.png"), "none");
    CHECK_EQ(find(""), "none");
}

// Without a finder, as a render's test has, a picture isn't there; a
// layout without pictures doesn't need one.
void test_layout_images_without_finder()
{
    auto const without = layout_images(decoded(R"({ "elements": [ { "type": "text", "text": "a" } ] })"), "/layouts", ImageFinder { });
    CHECK(without && without->empty());
    auto const with = layout_images(decoded(R"({ "elements": [ { "type": "image", "src": "a.png" } ] })"), "/layouts", ImageFinder { });
    CHECK(!with.has_value());
    if (!with) {
        CHECK(with.error().to_string().starts_with("element 1: the picture `a.png` isn't there"));
    }
}

int main()
{
    test_find_image_paths();
    test_layout_images_without_finder();
    test_load_and_reload();
    test_errors_and_recovering();
    test_overlay_layouts();
    test_chosen_layout();
    test_builtin_layout("cycling-basic", 11);
    test_builtin_layout("running-basic", 9);
    test_builtin_layout("cycling-dashboard", 10);
    test_dashboard_map();
    test_dashboard_title_card();
    test_dashboard_groups();
    test_dashboard_icons();
    test_find_image();
    test_layout_images();
    test_layout_images_in_groups();
    test_layout_fonts();
    test_missing_file();
    test_error_names_file_element_and_property();
    test_no_layout_directories();
    test_chosen_layout_not_a_name();
    return ST::Test::test_result();
}
