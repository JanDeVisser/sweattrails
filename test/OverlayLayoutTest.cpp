/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Decoding overlay layouts, the style cascade, and placing elements.
 */

#include <format>
#include <string>
#include <vector>

#include <JSON.h>

#include <overlay/Layout.h>
#include <storage/Storage.h>

#include <TestApp.h>

using namespace ST;

namespace {

std::expected<OverlayLayout, LayoutError> decode(std::string_view json)
{
    auto const parsed = JSONValue::deserialize(json);
    if (!parsed) {
        return std::unexpected(LayoutError { .message = "not JSON" });
    }
    return decode_layout(*parsed);
}

// The error, or "ok".
std::string error_of(std::string_view json)
{
    auto const layout = decode(json);
    return layout ? std::string { "ok" } : layout.error().to_string();
}

// A layout with one element, given as its JSON object.
std::string with_element(std::string_view element)
{
    return std::format(R"({{ "elements": [ {} ] }})", element);
}

std::string show(Color c)
{
    return std::format("{} {} {} {}", c.r, c.g, c.b, c.a);
}

std::string show(Rectangle r)
{
    return std::format("{} {} {} {}", r.x, r.y, r.width, r.height);
}

void test_cascade()
{
    auto const base = MUST_EVAL(JSONValue::deserialize(R"({"font_size": 48, "shadow": {"color": "#000000", "offset": [2, 2]}, "font": "a"})"));
    auto const over = MUST_EVAL(JSONValue::deserialize(R"({"font_size": 22, "shadow": {"offset": [3, 3]}})"));
    auto const merged = cascade(base, over);
    CHECK_EQ(merged.try_get<int>("font_size").value_or(0), 22);
    CHECK_EQ(merged.try_get<std::string>("font").value_or(""), "a");
    auto const shadow = merged.get("shadow").value_or(JSONValue { });
    // Objects merge, and arrays are replaced, not appended to.
    CHECK_EQ(shadow.try_get<std::string>("color").value_or(""), "#000000");
    CHECK_EQ(shadow.get("offset").value_or(JSONValue { }).size(), 2uz);
    // Null replaces an object, to turn the shadow off.
    auto const off = cascade(merged, MUST_EVAL(JSONValue::deserialize(R"({"shadow": null})")));
    CHECK(off.get("shadow").value_or(JSONValue::object()).is_null());
}

// Defaults, then the styles in order, then the element's own properties.
void test_styles()
{
    auto const layout = decode(R"({
        "defaults": { "font": "Inter-Bold.ttf", "font_size": 48, "color": "#ffffff",
                      "shadow": { "color": "#00000080", "offset": [2, 2] } },
        "styles": { "label": { "font_size": 22, "color": "#ffffffb0", "font": "Inter-Regular.ttf" },
                    "big": { "font_size": 64 } },
        "elements": [
            { "type": "text", "text": "a" },
            { "type": "text", "text": "b", "style": "label" },
            { "type": "text", "text": "c", "style": ["label", "big"], "color": "#ff0000" },
            { "type": "text", "text": "d", "style": "label", "shadow": null }
        ]
    })");
    CHECK(layout.has_value());
    if (!layout || layout->elements.size() != 4) {
        return;
    }
    auto const style = [&layout](size_t ix) { return std::get<TextElement>(layout->elements[ix]).style; };
    CHECK_EQ(style(0).font, "Inter-Bold.ttf");
    CHECK_EQ(style(0).font_size, 48.0f);
    CHECK(style(0).shadow.has_value());
    if (style(0).shadow) {
        CHECK_EQ(show(style(0).shadow->color), "0 0 0 128");
        CHECK_EQ(style(0).shadow->offset.x, 2.0f);
    }
    CHECK_EQ(style(1).font, "Inter-Regular.ttf");
    CHECK_EQ(style(1).font_size, 22.0f);
    CHECK_EQ(show(style(1).color), "255 255 255 176");
    CHECK_EQ(style(2).font_size, 64.0f);
    CHECK_EQ(show(style(2).color), "255 0 0 255");
    CHECK(!style(3).shadow.has_value());
}

void test_layout_properties()
{
    auto const layout = decode(R"({ "version": 1, "design_size": { "width": 1080, "height": 1920 }, "units": "imperial" })");
    CHECK(layout.has_value());
    if (layout) {
        CHECK_EQ(layout->design_size.x, 1080.0f);
        CHECK_EQ(layout->design_size.y, 1920.0f);
        CHECK(layout->units == Units::Imperial);
    }
    // The defaults.
    auto const plain = decode("{}");
    CHECK(plain.has_value());
    if (plain) {
        CHECK_EQ(plain->design_size.x, 1920.0f);
        CHECK(plain->units == Units::Metric);
        CHECK_EQ(plain->elements.size(), 0uz);
    }
}

void test_text()
{
    auto const layout = decode(with_element(R"({ "type": "text", "text": "Lap {lap} · {distance:.1f} {distance.unit}",
        "align": "right", "at": { "x": 40, "y": "10%", "anchor": "bottom-right" }, "size": { "width": 400, "height": 120 } })"));
    CHECK(layout.has_value());
    if (!layout || layout->elements.empty()) {
        return;
    }
    auto const &text = std::get<TextElement>(layout->elements[0]);
    CHECK(text.align == Align::Right);
    // Without an align, a text follows its anchor.
    auto const plain = decode(with_element(R"({ "type": "text", "text": "a" })"));
    CHECK(plain && !plain->elements.empty() && !std::get<TextElement>(plain->elements[0]).align.has_value());
    CHECK_EQ(text.text.parts.size(), 6uz);
    CHECK_EQ(text.at.x.value, 40.0);
    CHECK(!text.at.x.percent);
    CHECK_EQ(text.at.y.value, 10.0);
    CHECK(text.at.y.percent);
    CHECK(text.at.anchor == Anchor::BottomRight);
    CHECK(text.at.size.has_value());
    CHECK_EQ(layout_codepoints(*layout).contains(L'·'), true);
}

void test_value()
{
    auto const layout = decode(with_element(R"({ "type": "value", "field": "speed", "format": "{:.2f}",
        "unit": "mph", "unit_style": { "font_size": 28 }, "missing": "hide", "font_size": 64, "color": "#ffb703" })"));
    CHECK(layout.has_value());
    if (!layout || layout->elements.empty()) {
        return;
    }
    auto const &value = std::get<ValueElement>(layout->elements[0]);
    CHECK(value.field == OverlayField::speed);
    CHECK_EQ(value.spec, ".2f");
    CHECK(value.unit.has_value());
    if (value.unit) {
        CHECK_EQ(value.unit->name, "mph");
    }
    CHECK_EQ(value.style.font_size, 64.0f);
    // The unit's style is the value's, with its own properties over it.
    CHECK_EQ(value.unit_style.font_size, 28.0f);
    CHECK_EQ(show(value.unit_style.color), "255 183 3 255");
    CHECK(value.hide_when_missing);
    // The defaults: the layout's units, the field's spec, and "--".
    auto const plain = decode(with_element(R"({ "type": "value", "field": "power", "unit": "auto" })"));
    CHECK(plain.has_value());
    if (plain && !plain->elements.empty()) {
        auto const &p = std::get<ValueElement>(plain->elements[0]);
        CHECK(!p.unit.has_value());
        CHECK_EQ(p.spec, "");
        CHECK_EQ(p.missing, "--");
        CHECK(!p.hide_when_missing);
    }
}

// Elements of later slices are skipped and counted, and box styles, which
// they use, aren't errors on a text or a value.
void test_later_elements()
{
    auto const layout = decode(R"({
        "styles": { "panel": { "background": "#00000060", "radius": 16, "padding": 16 } },
        "elements": [
            { "type": "map", "view": "follow", "zoom": 15, "anything": true },
            { "type": "text", "text": "a", "style": "panel", "visible": { "from": 5 }, "fade": 0.5 },
            { "type": "group", "children": [] }
        ]
    })");
    CHECK(layout.has_value());
    if (layout) {
        CHECK_EQ(layout->elements.size(), 1uz);
        CHECK_EQ(layout->skipped, 2uz);
    }
}

void test_errors()
{
    CHECK_EQ(error_of("[]"), "the layout isn't a JSON object");
    CHECK_EQ(error_of(R"({ "colour": "#ffffff" })"), "\"colour\" isn't a property of a layout");
    CHECK_EQ(error_of(R"({ "version": 2 })"), "\"version\": only version 1 is known");
    CHECK_EQ(error_of(R"({ "units": "nautical" })"), "\"units\" isn't metric or imperial");
    CHECK_EQ(error_of(R"({ "design_size": { "width": 1920 } })"), "\"design_size\" needs a width and a height");
    CHECK_EQ(error_of(R"({ "elements": [ { "type": "text", "text": "a" }, { "type": "value", "field": "speed", "colour": "#fff" } ] })"),
        "element 2: \"colour\" isn't a property of a value");
    CHECK_EQ(error_of(with_element(R"({ "type": "txt" })")), "element 1: \"type\": `txt` isn't an element type");
    CHECK_EQ(error_of(with_element(R"({ "text": "a" })")), "element 1: it has no \"type\"");
    CHECK_EQ(error_of(with_element(R"({ "type": "text" })")), "element 1: a text needs a \"text\"");
    CHECK_EQ(error_of(with_element(R"({ "type": "text", "text": "a", "color": "white" })")),
        "element 1: \"color\": `white` isn't a color: give one as \"#rrggbb\" or \"#rrggbbaa\"");
    CHECK_EQ(error_of(with_element(R"({ "type": "text", "text": "{hr}" })")), "element 1: \"text\": `hr` isn't a field");
    CHECK_EQ(error_of(with_element(R"({ "type": "text", "text": "a", "style": "nope" })")), "element 1: \"style\": there's no style `nope`");
    CHECK_EQ(error_of(with_element(R"({ "type": "text", "text": "a", "at": { "x": "half" } })")),
        "element 1: \"x\" isn't a number or a percentage, such as \"50%\"");
    CHECK_EQ(error_of(with_element(R"({ "type": "text", "text": "a", "at": { "anchor": "middle" } })")),
        "element 1: \"anchor\" isn't one of top-left, top, top-right, left, center, right, bottom-left, bottom, bottom-right");
    CHECK_EQ(error_of(with_element(R"({ "type": "text", "text": "a", "shadow": { "offset": [1] } })")),
        "element 1: \"shadow.offset\" isn't a pair of numbers, such as [2, 2]");
    CHECK_EQ(error_of(with_element(R"({ "type": "value" })")), "element 1: a value needs a \"field\"");
    CHECK_EQ(error_of(with_element(R"({ "type": "value", "field": "hr" })")), "element 1: \"field\": `hr` isn't a field");
    CHECK(decode(with_element(R"({ "type": "value", "field": "lap" })")).has_value());
    CHECK_EQ(error_of(with_element(R"({ "type": "value", "field": "elapsed", "format": "{:.1f}" })")),
        "element 1: \"format\": `{:.1f}` isn't a format for elapsed");
    CHECK_EQ(error_of(with_element(R"({ "type": "value", "field": "speed", "format": "{:.1q}" })")),
        "element 1: \"format\": `{:.1q}` isn't a format for speed");
    CHECK_EQ(error_of(with_element(R"({ "type": "value", "field": "speed", "format": ".1f" })")),
        "element 1: \"format\": `.1f` isn't a format such as \"{:.1f}\"");
    CHECK_EQ(error_of(with_element(R"({ "type": "value", "field": "speed", "unit": "km" })")),
        "element 1: \"unit\": speed can't be shown in `km`");
    CHECK_EQ(error_of(with_element(R"({ "type": "value", "field": "speed", "unit_style": { "align": "left" } })")),
        "element 1: \"align\" isn't a property of \"unit_style\"");
}

// The sign picks the edge, and the anchor the element's point that's put
// there. A 1920 x 1080 design on a canvas half its size: scale 0.5.
void test_place()
{
    auto const canvas = Vector2 { .x = 960, .y = 540 };
    auto const scale = layout_scale(Vector2 { .x = 1920, .y = 1080 }, canvas);
    CHECK_EQ(scale, 0.5f);
    auto const size = Vector2 { .x = 100, .y = 50 };
    auto const at = [&](Coordinate x, Coordinate y, Anchor anchor) {
        return show(place(Placement { .x = x, .y = y, .anchor = anchor }, size, canvas, scale));
    };
    CHECK_EQ(at({ 40 }, { 40 }, Anchor::TopLeft), "20 20 100 50");
    CHECK_EQ(at({ 40 }, { -40 }, Anchor::BottomLeft), "20 470 100 50");
    CHECK_EQ(at({ -40 }, { -40 }, Anchor::BottomRight), "840 470 100 50");
    CHECK_EQ(at({ -40 }, { 40 }, Anchor::TopRight), "840 20 100 50");
    CHECK_EQ(at({ 50, true }, { -40 }, Anchor::Bottom), "430 470 100 50");
    CHECK_EQ(at({ 50, true }, { 50, true }, Anchor::Center), "430 245 100 50");
    CHECK_EQ(at({ 0 }, { 50, true }, Anchor::Left), "0 245 100 50");
    CHECK_EQ(at({ -10, true }, { 0 }, Anchor::Top), "814 0 100 50");
    CHECK_EQ(at({ -10 }, { 50, true }, Anchor::Right), "855 245 100 50");
    // A canvas of another shape: the smaller factor.
    CHECK_EQ(layout_scale(Vector2 { .x = 1920, .y = 1080 }, Vector2 { .x = 3840, .y = 1080 }), 1.0f);
}

// The index counts every element, also those of later slices that are
// skipped, so that it's the one the user finds in the file.
void test_error_index_counts_skipped()
{
    CHECK_EQ(error_of(R"({ "elements": [ { "type": "map" }, { "type": "profile" }, { "type": "text", "text": "a", "colour": "#fff" } ] })"),
        "element 3: \"colour\" isn't a property of a text");
}

// A typo in the defaults or a style is an error there, not in the first
// element that takes it.
void test_typos_in_defaults_and_styles()
{
    CHECK_EQ(error_of(R"({ "defaults": { "colour": "#fff" }, "elements": [ { "type": "text", "text": "a" } ] })"),
        "\"colour\" isn't a property of the defaults");
    CHECK_EQ(error_of(R"({ "styles": { "big": { "fontsize": 64 } }, "elements": [ { "type": "text", "text": "a", "style": "big" } ] })"),
        "\"fontsize\" isn't a property of the style `big`");
    // Also in a style nothing uses.
    CHECK_EQ(error_of(R"({ "styles": { "big": { "fontsize": 64 } } })"), "\"fontsize\" isn't a property of the style `big`");
    CHECK_EQ(error_of(R"({ "styles": { "big": 64 } })"), "the style `big` isn't an object");
    CHECK_EQ(error_of(with_element(R"({ "type": "text", "text": "a", "style": ["big", 3] })")), "element 1: \"style\" isn't a string");
    CHECK_EQ(error_of(with_element(R"({ "type": "text", "text": "a", "style": 3 })")), "element 1: \"style\" isn't a name or a list of names");
    // A text's property in the defaults doesn't make a value an error, nor
    // a value's a text, nor a later element's either.
    CHECK_EQ(error_of(R"({ "defaults": { "align": "right", "missing": "-", "line": { "width": 3 } },
        "elements": [ { "type": "text", "text": "a" }, { "type": "value", "field": "speed" } ] })"),
        "ok");
    // Later slices' value properties are accepted.
    CHECK_EQ(error_of(with_element(R"({ "type": "value", "field": "heart_rate", "color_by": { "thresholds": [[0, "#8ecae6"]] } })")), "ok");
}

// Through decode_layout(), not only cascade(): a style's offset replaces the
// default's, and the default's shadow color stays.
void test_cascade_in_layout()
{
    auto const layout = decode(R"({
        "defaults": { "shadow": { "color": "#ff000080", "offset": [2, 2] } },
        "styles": { "far": { "shadow": { "offset": [5, 6] } } },
        "elements": [ { "type": "text", "text": "a", "style": "far" } ]
    })");
    CHECK(layout.has_value());
    if (!layout || layout->elements.empty()) {
        return;
    }
    auto const &shadow = std::get<TextElement>(layout->elements[0]).style.shadow;
    CHECK(shadow.has_value());
    if (shadow) {
        CHECK_EQ(shadow->offset.x, 5.0f);
        CHECK_EQ(shadow->offset.y, 6.0f);
        CHECK_EQ(show(shadow->color), "255 0 0 128");
    }
}

// The unit's style is over the value's, and objects merge there as in the
// rest of the cascade: a unit_style shadow with only an offset keeps the
// value's shadow color.
void test_unit_style_shadow_merges()
{
    auto const layout = decode(R"({
        "defaults": { "shadow": { "color": "#ff000080", "offset": [2, 2] } },
        "elements": [ { "type": "value", "field": "speed", "unit_style": { "shadow": { "offset": [1, 1] } } } ]
    })");
    CHECK(layout.has_value());
    if (!layout || layout->elements.empty()) {
        return;
    }
    auto const &shadow = std::get<ValueElement>(layout->elements[0]).unit_style.shadow;
    CHECK(shadow.has_value());
    if (shadow) {
        CHECK_EQ(shadow->offset.x, 1.0f);
        CHECK_EQ(show(shadow->color), "255 0 0 128");
    }
}

void test_coordinates()
{
    auto const x_of = [](std::string_view x) -> std::string {
        auto const layout = decode(with_element(std::format(R"({{ "type": "text", "text": "a", "at": {{ "x": {} }} }})", x)));
        if (!layout) {
            return layout.error().to_string();
        }
        auto const &c = std::get<TextElement>(layout->elements[0]).at.x;
        return std::format("{}{}", c.value, c.percent ? "%" : "");
    };
    CHECK_EQ(x_of("40"), "40");
    CHECK_EQ(x_of("-40.5"), "-40.5");
    CHECK_EQ(x_of(R"("50%")"), "50%");
    CHECK_EQ(x_of(R"("-10%")"), "-10%");
    CHECK_EQ(x_of(R"("12.5%")"), "12.5%");
    auto const bad = "element 1: \"x\" isn't a number or a percentage, such as \"50%\"";
    CHECK_EQ(x_of(R"("%")"), bad);
    CHECK_EQ(x_of(R"("50 %")"), bad);
    CHECK_EQ(x_of(R"("50")"), bad);
    CHECK_EQ(x_of(R"("%50")"), bad);
    CHECK_EQ(x_of("true"), bad);
}

// A percentage is of the canvas, whatever the scale; pixels are scaled.
void test_place_scaled()
{
    auto const canvas = Vector2 { .x = 1280, .y = 720 };
    auto const scale = layout_scale(Vector2 { .x = 1920, .y = 1080 }, canvas);
    auto const size = Vector2 { .x = 30, .y = 60 };
    auto const box = place(Placement { .x = { -25, true }, .y = { -60 }, .anchor = Anchor::BottomRight }, size, canvas, scale);
    CHECK_EQ(box.x, 1280.0f - 320.0f - 30.0f);
    CHECK_EQ(box.y, 720.0f - 40.0f - 60.0f);
    // The default anchor is top-left, at 0, 0.
    CHECK_EQ(show(place(Placement { }, size, canvas, scale)), "0 0 30 60");
}

void test_value_formats()
{
    auto const spec_of = [](std::string_view field, std::string_view format) -> std::string {
        auto const layout = decode(with_element(std::format(R"({{ "type": "value", "field": "{}", "format": "{}" }})", field, format)));
        if (!layout) {
            return layout.error().to_string();
        }
        return "spec " + std::get<ValueElement>(layout->elements[0]).spec;
    };
    CHECK_EQ(spec_of("speed", "{}"), "spec ");
    CHECK_EQ(spec_of("speed", "{:}"), "spec ");
    CHECK_EQ(spec_of("speed", "{:>6.1f}"), "spec >6.1f");
    CHECK_EQ(spec_of("speed", "{0:.1f}"), "element 1: \"format\": `{0:.1f}` isn't a format such as \"{:.1f}\"");
    CHECK_EQ(spec_of("speed", "{:.1f} km"), "element 1: \"format\": `{:.1f} km` isn't a format such as \"{:.1f}\"");
    CHECK_EQ(spec_of("speed", "{:d}"), "element 1: \"format\": `{:d}` isn't a format for speed");
    CHECK_EQ(spec_of("speed", "{:{}}"), "element 1: \"format\": `{:{}}` isn't a format for speed");
    // Duration specs come in slice 3, and aren't errors now.
    CHECK_EQ(spec_of("elapsed", "{:hms}"), "spec hms");
}

void test_sizes()
{
    CHECK_EQ(error_of(with_element(R"({ "type": "text", "text": "a", "font_size": 0 })")), "element 1: \"font_size\" isn't more than 0");
    CHECK_EQ(error_of(R"({ "design_size": { "width": 0, "height": 1080 } })"), "\"design_size\" isn't more than 0 wide and high");
    CHECK_EQ(error_of(with_element(R"({ "type": "text", "text": "a", "size": { "width": 400 } })")), "element 1: \"size\" needs a width and a height");
}

// The characters besides ASCII that the fonts need: in texts, in missing
// texts, and the "--" of a text's missing fields, which is ASCII.
void test_codepoints()
{
    auto const layout = decode(R"({ "elements": [
        { "type": "text", "text": "{temperature}°" },
        { "type": "value", "field": "power", "missing": "—" }
    ] })");
    CHECK(layout.has_value());
    if (!layout) {
        return;
    }
    auto const codepoints = layout_codepoints(*layout);
    CHECK(codepoints.contains(0xb0));
    CHECK(codepoints.contains(0x2014));
    CHECK_EQ(codepoints.size(), 2uz);
}

// Each element knows its place in the file, counting the skipped ones.
void test_element_numbers()
{
    auto const layout = decode(R"({ "elements": [
        { "type": "text", "text": "a" }, { "type": "profile" }, { "type": "value", "field": "speed" } ] })");
    CHECK(layout.has_value());
    if (layout && layout->elements.size() == 2) {
        CHECK_EQ(element_number(layout->elements[0]), 1uz);
        CHECK_EQ(element_number(layout->elements[1]), 3uz);
    }
}

// Content smaller than its box goes where the anchor is in the box; a
// text's align picks the horizontal place.
void test_place_in_box()
{
    auto const box = Vector2 { .x = 400, .y = 120 };
    auto const size = Vector2 { .x = 100, .y = 60 };
    auto const in = [&](Anchor anchor, std::optional<Align> align = { }) {
        auto const at = place_in_box(box, size, anchor, align);
        return std::format("{} {}", at.x, at.y);
    };
    CHECK_EQ(in(Anchor::TopLeft), "0 0");
    CHECK_EQ(in(Anchor::BottomLeft), "0 60");
    CHECK_EQ(in(Anchor::BottomRight), "300 60");
    CHECK_EQ(in(Anchor::Center), "150 30");
    CHECK_EQ(in(Anchor::Top), "150 0");
    CHECK_EQ(in(Anchor::BottomLeft, Align::Right), "300 60");
    CHECK_EQ(in(Anchor::BottomRight, Align::Left), "0 60");
    CHECK_EQ(in(Anchor::TopLeft, Align::Center), "150 0");
    // Content as big as the box fills it.
    CHECK_EQ(std::format("{}", place_in_box(box, box, Anchor::BottomRight).x), "0");
}
// The units' degrees are there, though the layout doesn't spell them out.
void test_codepoints_of_units()
{
    auto const layout = decode(with_element(R"({ "type": "value", "field": "temperature" })"));
    CHECK(layout.has_value());
    if (layout) {
        CHECK(layout_codepoints(*layout).contains(0xb0));
    }
}

void test_grade_window()
{
    auto const layout = decode(with_element(R"({ "type": "value", "field": "grade", "window": 20 })"));
    CHECK(layout.has_value());
    if (layout) {
        CHECK_EQ(std::get<ValueElement>(layout->elements[0]).window.value_or(0), 20.0);
    }
    CHECK_EQ(error_of(with_element(R"({ "type": "value", "field": "speed", "window": 20 })")), "element 1: \"window\" is only for grade");
    CHECK_EQ(error_of(with_element(R"({ "type": "value", "field": "grade", "window": 5 })")), "element 1: \"window\" is from 10 to 200 m");
    CHECK_EQ(error_of(with_element(R"({ "type": "value", "field": "grade", "window": 201 })")), "element 1: \"window\" is from 10 to 200 m");
    CHECK_EQ(error_of(with_element(R"({ "type": "value", "field": "grade", "window": "short" })")), "element 1: \"window\" isn't a number");
}

void test_color_by()
{
    auto const layout = decode(with_element(R"({ "type": "value", "field": "power",
        "color_by": { "thresholds": [[0, "#8ecae6"], [220, "#f9c74f"], [260, "#f94144"]] } })"));
    CHECK(layout.has_value());
    if (!layout) {
        return;
    }
    auto const &value = std::get<ValueElement>(layout->elements[0]);
    CHECK_EQ(value.thresholds.size(), 3uz);
    if (value.thresholds.size() == 3) {
        CHECK_EQ(value.thresholds[1].from, 220.0);
        CHECK_EQ(show(value.thresholds[2].color), "249 65 68 255");
    }
}

void test_color_by_errors()
{
    auto const value = [](std::string_view field, std::string_view color_by) {
        return error_of(with_element(std::format(R"({{ "type": "value", "field": "{}", "color_by": {} }})", field, color_by)));
    };
    auto const usage = std::string { "element 1: \"color_by\" needs \"thresholds\", such as [[0, \"#8ecae6\"], [150, \"#f94144\"]]" };
    CHECK_EQ(value("power", "[]"), usage);
    CHECK_EQ(value("power", "{}"), usage);
    CHECK_EQ(value("power", R"({ "thresholds": [] })"), usage);
    CHECK_EQ(value("power", R"({ "thresholds": [[0]] })"), usage);
    CHECK_EQ(value("power", R"({ "thresholds": [["0", "#ffffff"]] })"), usage);
    CHECK_EQ(value("power", R"({ "thresholds": [[0, "red"]] })"),
        "element 1: \"color_by\": `red` isn't a color: give one as \"#rrggbb\" or \"#rrggbbaa\"");
    CHECK_EQ(value("power", R"({ "thresholds": [[200, "#ffffff"], [100, "#000000"]] })"), "element 1: \"color_by\": the thresholds have to go up");
    CHECK_EQ(value("power", R"({ "thresholds": [[200, "#ffffff"], [200, "#000000"]] })"), "element 1: \"color_by\": the thresholds have to go up");
    CHECK_EQ(value("heart_rate", R"({ "zones": "hr", "colors": ["#ffffff"] })"),
        "element 1: \"color_by\": zones need an athlete profile, which there isn't yet: give \"thresholds\"");
    CHECK_EQ(value("power", R"({ "thresholds": [[0, "#ffffff"]], "steps": 3 })"), "element 1: \"steps\" isn't a property of \"color_by\"");
    // Only numbers: not a duration, such as pace, a clock, a date, a balance or the title.
    for (auto const field : { "pace", "elapsed", "time_of_day", "date", "lr_balance", "title" }) {
        CHECK_EQ(value(field, R"({ "thresholds": [[0, "#ffffff"]] })"), std::format("element 1: \"color_by\": {} isn't a number", field));
    }
    // Only values.
    CHECK_EQ(error_of(with_element(R"({ "type": "text", "text": "{power}", "color_by": { "thresholds": [[0, "#ffffff"]] } })")),
        "element 1: \"color_by\" isn't a property of a text");
}

void test_threshold_color()
{
    std::vector<Threshold> const thresholds { { .from = 100, .color = RED }, { .from = 200, .color = BLUE } };
    CHECK_EQ(show(threshold_color(thresholds, 50.0, WHITE)), show(WHITE));
    CHECK_EQ(show(threshold_color(thresholds, 100.0, WHITE)), show(RED));
    CHECK_EQ(show(threshold_color(thresholds, 199.9, WHITE)), show(RED));
    CHECK_EQ(show(threshold_color(thresholds, 250.0, WHITE)), show(BLUE));
    CHECK_EQ(show(threshold_color(thresholds, std::nullopt, WHITE)), show(WHITE));
    CHECK_EQ(show(threshold_color({ }, 250.0, WHITE)), show(WHITE));
}

// What a value element shows: in the unit shown, colored by it, the unit
// in its own color; a grade over its own window.
void test_shown_value()
{
    auto const layout = decode(R"({ "elements": [
        { "type": "value", "field": "speed", "color": "#ffffff",
          "color_by": { "thresholds": [[30, "#ff0000"]] } },
        { "type": "value", "field": "power", "missing": "hide" },
        { "type": "value", "field": "title" }
    ] })");
    CHECK(layout.has_value());
    if (!layout) {
        return;
    }
    FieldValues values { .title = "Col du Galibier" };
    values.values[static_cast<size_t>(OverlayField::speed)] = 10.0; // 36 km/h, 22.4 mph
    auto const &speed = std::get<ValueElement>(layout->elements[0]);
    auto const  metric = shown_value(speed, values, Units::Metric);
    CHECK(metric.has_value());
    if (metric) {
        CHECK_EQ(metric->number, "36.0");
        CHECK_EQ(metric->unit, "km/h");
        CHECK_EQ(show(metric->color), "255 0 0 255");
    }
    // 30 is in mph now: below it.
    auto const imperial = shown_value(speed, values, Units::Imperial);
    CHECK(imperial.has_value());
    if (imperial) {
        CHECK_EQ(imperial->number, "22.4");
        CHECK_EQ(show(imperial->color), "255 255 255 255");
    }
    CHECK(!shown_value(std::get<ValueElement>(layout->elements[1]), values, Units::Metric).has_value());
    auto const title = shown_value(std::get<ValueElement>(layout->elements[2]), values, Units::Metric);
    CHECK(title.has_value());
    if (title) {
        CHECK_EQ(title->number, "Col du Galibier");
        CHECK_EQ(title->unit, "");
    }
    // Missing: the missing text, no unit, the element's color.
    values.values[static_cast<size_t>(OverlayField::speed)].reset();
    auto const missing = shown_value(speed, values, Units::Metric);
    CHECK(missing.has_value());
    if (missing) {
        CHECK_EQ(missing->number, "--");
        CHECK_EQ(missing->unit, "");
        CHECK_EQ(show(missing->color), "255 255 255 255");
    }
}

// Thresholds on a temperature are in the unit shown, offset included:
// 20 °C is 68 °F, above 60 °F, but below 60 °C.
void test_thresholds_in_fahrenheit()
{
    auto const layout = decode(R"({ "elements": [
        { "type": "value", "field": "temperature", "color": "#ffffff",
          "color_by": { "thresholds": [[60, "#ff0000"]] } },
        { "type": "value", "field": "temperature", "unit": "°F", "color": "#ffffff",
          "color_by": { "thresholds": [[60, "#ff0000"]] } }
    ] })");
    CHECK(layout.has_value());
    if (!layout) {
        return;
    }
    FieldValues values { };
    values.values[static_cast<size_t>(OverlayField::temperature)] = 20.0;
    auto const &own_units = std::get<ValueElement>(layout->elements[0]);
    auto const  metric = shown_value(own_units, values, Units::Metric);
    auto const  imperial = shown_value(own_units, values, Units::Imperial);
    auto const  explicit_f = shown_value(std::get<ValueElement>(layout->elements[1]), values, Units::Metric);
    CHECK(metric && imperial && explicit_f);
    if (metric && imperial && explicit_f) {
        CHECK_EQ(show(metric->color), "255 255 255 255");
        CHECK_EQ(imperial->number, "68");
        CHECK_EQ(imperial->unit, "°F");
        CHECK_EQ(show(imperial->color), "255 0 0 255");
        CHECK_EQ(show(explicit_f->color), "255 0 0 255");
    }
    // -40 is the same in both: below 0 in either.
    values.values[static_cast<size_t>(OverlayField::temperature)] = -40.0;
    auto const cold = shown_value(own_units, values, Units::Imperial);
    CHECK(cold.has_value());
    if (cold) {
        CHECK_EQ(cold->number, "-40");
    }
}

// The color goes with the number as it's shown: 259.6 W shows as 260,
// which a threshold at 260 says is red. Rendering interpolates between
// records, so this happens on many frames around a threshold.
void test_threshold_at_the_shown_number()
{
    auto const layout = decode(with_element(R"({ "type": "value", "field": "power", "color": "#ffffff",
        "color_by": { "thresholds": [[220, "#00ff00"], [260, "#ff0000"]] } })"));
    CHECK(layout.has_value());
    if (!layout) {
        return;
    }
    FieldValues values { };
    values.values[static_cast<size_t>(OverlayField::power)] = 259.6;
    auto const shown = shown_value(std::get<ValueElement>(layout->elements[0]), values, Units::Metric);
    CHECK(shown.has_value());
    if (shown) {
        CHECK_EQ(shown->number, "260");
        CHECK_EQ(show(shown->color), "255 0 0 255");
    }
    // Padded and signed, it still reads as 260.
    auto const padded = decode(with_element(R"({ "type": "value", "field": "power", "format": "{:>+8.0f}",
        "color_by": { "thresholds": [[220, "#00ff00"], [260, "#ff0000"]] } })"));
    CHECK(padded.has_value());
    if (padded) {
        auto const p = shown_value(std::get<ValueElement>(padded->elements[0]), values, Units::Metric);
        CHECK(p.has_value() && p->number == "    +260");
        CHECK(p.has_value() && show(p->color) == "255 0 0 255");
    }
}

// A grade element with its own window uses it, from the frame's source;
// without a source, as in a FieldValues made by hand, it's missing.
void test_shown_grade_window()
{
    auto const layout = decode(R"({ "elements": [
        { "type": "value", "field": "grade", "window": 100, "format": "{:.1f}" },
        { "type": "value", "field": "grade", "format": "{:.1f}" },
        { "type": "value", "field": "grade", "window": 100, "missing": "hide" }
    ] })");
    CHECK(layout.has_value());
    if (!layout) {
        return;
    }
    Storage  storage { };
    Activity activity { storage, ActivityID { } };
    // Flat to 100 m, 20% to 140 m, flat to 300 m.
    for (auto d = 0u; d <= 300; d += 5) {
        auto const altitude = (d <= 100) ? 0.0 : (d <= 140) ? (d - 100) * 0.2
                                                            : 8.0;
        activity.records.push_back(record { .timestamp = DateTime { .timestamp = d / 5 }, .altitude = static_cast<f32>(altitude), .distance = static_cast<f32>(d) });
    }
    OverlaySource const source { activity };
    auto const          values = values_at_record(source, 24); // 120 m
    auto const          wide = shown_value(std::get<ValueElement>(layout->elements[0]), values, Units::Metric);
    auto const          narrow = shown_value(std::get<ValueElement>(layout->elements[1]), values, Units::Metric);
    CHECK(wide && narrow);
    if (wide && narrow) {
        CHECK_EQ(wide->number, "8.0");
        CHECK_EQ(wide->unit, "%");
        CHECK_EQ(narrow->number, "20.0");
    }
    // Near the start the wide window has no grade, and hides.
    CHECK(!shown_value(std::get<ValueElement>(layout->elements[2]), values_at_record(source, 4), Units::Metric).has_value());
    CHECK(!shown_value(std::get<ValueElement>(layout->elements[2]), FieldValues { }, Units::Metric).has_value());
}

// A value's "format" is checked for the field's kind.
void test_value_formats_per_kind()
{
    auto const value = [](std::string_view field, std::string_view format) {
        return error_of(with_element(std::format(R"({{ "type": "value", "field": "{}", "format": "{}" }})", field, format)));
    };
    CHECK_EQ(value("pace", "{:ms}"), "ok");
    CHECK_EQ(value("pace", "{:hms}"), "element 1: \"format\": `{:hms}` isn't a format for pace");
    CHECK_EQ(value("elapsed", "{:ms}"), "ok");
    CHECK_EQ(value("time_of_day", "{:hms}"), "ok");
    CHECK_EQ(value("time_of_day", "{:%H:%M}"), "element 1: \"format\": `{:%H:%M}` isn't a format for time_of_day");
    CHECK_EQ(value("date", "{:%d %b}"), "ok");
    CHECK_EQ(value("date", "{:%Q}"), "element 1: \"format\": `{:%Q}` isn't a format for date");
    CHECK_EQ(value("lr_balance", "{:.1f}"), "ok");
    CHECK_EQ(value("title", "{}"), "ok");
    CHECK_EQ(value("title", "{:>10}"), "element 1: \"format\": `{:>10}` isn't a format for title");
    CHECK_EQ(value("altitude", "{:hms}"), "element 1: \"format\": `{:hms}` isn't a format for altitude");
}

}

// A gauge's shape, bounds and colors, and what it takes when it says
// nothing: its color, and the track at a fifth of its alpha.
void test_gauge()
{
    auto const layout = decode(R"({ "elements": [
        { "type": "gauge", "field": "power", "shape": "arc", "min": 0, "max": 400,
          "track": "#ffffff30", "fill": "#ffb703", "thickness": 14,
          "color_by": { "thresholds": [[0, "#8ecae6"], [260, "#f94144"]] },
          "size": { "width": 200, "height": 200 } },
        { "type": "gauge", "field": "speed", "unit": "mph", "color": "#ff0000", "max": "auto",
          "size": { "width": 200, "height": 20 } }
    ] })");
    CHECK(layout.has_value());
    if (!layout || layout->elements.size() != 2) {
        return;
    }
    auto const &arc = std::get<GaugeElement>(layout->elements[0]);
    CHECK(arc.field == OverlayField::power);
    CHECK(arc.shape == GaugeShape::Arc);
    CHECK_EQ(arc.min.value_or(-1), 0.0);
    CHECK_EQ(arc.max.value_or(-1), 400.0);
    CHECK_EQ(show(arc.track), "255 255 255 48");
    CHECK_EQ(show(arc.fill), "255 183 3 255");
    CHECK_EQ(arc.thickness.value_or(0), 14.0f);
    CHECK_EQ(arc.thresholds.size(), 2uz);
    CHECK_EQ(arc.number, 1uz);

    auto const &bar = std::get<GaugeElement>(layout->elements[1]);
    CHECK(bar.shape == GaugeShape::Bar);
    CHECK(!bar.min);
    CHECK(!bar.max);
    CHECK_EQ(bar.unit ? bar.unit->name : std::string { "none" }, "mph");
    CHECK_EQ(show(bar.fill), "255 0 0 255");
    CHECK_EQ(show(bar.track), "255 0 0 51");
    CHECK(!bar.thickness);
    CHECK_EQ(bar.number, 2uz);
}

// A graph's axis, window, stroke and cursor, and its defaults: a 2 px line
// in its color, all of the activity, no fill and no cursor.
void test_graph()
{
    auto const layout = decode(R"({ "elements": [
        { "type": "graph", "field": "heart_rate", "x_axis": "time", "window": 60,
          "line": { "color": "#f94144", "width": 3 }, "fill": "#f9414430",
          "cursor": { "color": "#ffffff", "radius": 8 }, "min": 80, "max": "auto",
          "size": { "width": 400, "height": 100 } },
        { "type": "graph", "field": "altitude", "x_axis": "distance", "window": "all", "color": "#00ff00",
          "line": { "width": 4 }, "cursor": { }, "size": { "width": 400, "height": 100 } },
        { "type": "graph", "field": "speed", "color": "#0000ff", "size": { "width": 400, "height": 100 } }
    ] })");
    CHECK(layout.has_value());
    if (!layout || layout->elements.size() != 3) {
        return;
    }
    auto const &hr = std::get<GraphElement>(layout->elements[0]);
    CHECK(hr.field == OverlayField::heart_rate);
    CHECK(hr.axis == GraphAxis::Time);
    CHECK_EQ(hr.window.value_or(0), 60.0);
    CHECK_EQ(show(hr.line.color), "249 65 68 255");
    CHECK_EQ(hr.line.width, 3.0f);
    CHECK_EQ(show(hr.fill.value_or(BLANK)), "249 65 68 48");
    CHECK(hr.cursor.has_value());
    if (hr.cursor) {
        CHECK_EQ(show(hr.cursor->color), "255 255 255 255");
        CHECK_EQ(hr.cursor->radius, 8.0f);
    }
    CHECK_EQ(hr.min.value_or(-1), 80.0);
    CHECK(!hr.max);

    // A stroke that gives only a width keeps the color; a cursor without a
    // color is the line's.
    auto const &altitude = std::get<GraphElement>(layout->elements[1]);
    CHECK(altitude.axis == GraphAxis::Distance);
    CHECK(!altitude.window);
    CHECK_EQ(show(altitude.line.color), "0 255 0 255");
    CHECK_EQ(altitude.line.width, 4.0f);
    CHECK_EQ(show(altitude.cursor.value_or(Cursor { }).color), "0 255 0 255");

    auto const &speed = std::get<GraphElement>(layout->elements[2]);
    CHECK(speed.axis == GraphAxis::Time);
    CHECK(!speed.window);
    CHECK_EQ(show(speed.line.color), "0 0 255 255");
    CHECK_EQ(speed.line.width, 2.0f);
    CHECK(!speed.fill);
    CHECK(!speed.cursor);
    CHECK(!speed.min);
    CHECK(!speed.max);
}

void test_gauge_graph_errors()
{
    auto const sized = R"("size": { "width": 100, "height": 20 })";
    auto const gauge = [&sized](std::string_view rest) {
        return error_of(with_element(std::format(R"({{ "type": "gauge", {}, {} }})", sized, rest)));
    };
    auto const graph = [&sized](std::string_view rest) {
        return error_of(with_element(std::format(R"({{ "type": "graph", {}, {} }})", sized, rest)));
    };
    CHECK_EQ(error_of(with_element(R"({ "type": "gauge", "field": "power" })")), "element 1: a gauge needs a \"size\"");
    CHECK_EQ(error_of(with_element(R"({ "type": "graph", "field": "power" })")), "element 1: a graph needs a \"size\"");
    CHECK_EQ(gauge(R"("shape": "bar")"), "element 1: a gauge needs a \"field\"");
    CHECK_EQ(gauge(R"("field": "powr")"), "element 1: \"field\": `powr` isn't a field");
    CHECK_EQ(gauge(R"("field": "pace")"), "element 1: \"field\": a gauge shows a number, which pace isn't");
    CHECK_EQ(graph(R"("field": "elapsed")"), "element 1: \"field\": a graph shows a number, which elapsed isn't");
    CHECK_EQ(gauge(R"("field": "power", "unit": "mph")"), "element 1: \"unit\": power can't be shown in `mph`");
    CHECK_EQ(gauge(R"("field": "power", "shape": "round")"), "element 1: \"shape\" isn't bar, vbar or arc");
    CHECK_EQ(gauge(R"("field": "power", "min": 400, "max": 0)"), "element 1: \"min\" isn't less than \"max\"");
    CHECK_EQ(gauge(R"("field": "power", "min": 100, "max": 100)"), "element 1: \"min\" isn't less than \"max\"");
    CHECK_EQ(gauge(R"("field": "power", "max": "lots")"), "element 1: \"max\" isn't a number or \"auto\"");
    CHECK_EQ(gauge(R"("field": "power", "thickness": 0)"), "element 1: \"thickness\" isn't more than 0");
    CHECK_EQ(gauge(R"("field": "power", "font_size": 20)"), "element 1: \"font_size\" isn't a property of a gauge");
    CHECK_EQ(gauge(R"("field": "power", "window": 60)"), "element 1: \"window\" isn't a property of a gauge");
    CHECK_EQ(graph(R"("field": "power", "x_axis": "laps")"), "element 1: \"x_axis\" isn't time or distance");
    CHECK_EQ(graph(R"("field": "power", "window": 0)"), "element 1: \"window\" isn't more than 0, or \"all\"");
    CHECK_EQ(graph(R"("field": "power", "window": "some")"), "element 1: \"window\" isn't more than 0, or \"all\"");
    CHECK_EQ(graph(R"("field": "power", "line": "#fff")"),
        "element 1: \"line\" isn't a line, such as { \"color\": \"#ffffff\", \"width\": 2 }");
    CHECK_EQ(graph(R"("field": "power", "line": { "width": 0 })"), "element 1: \"line.width\" isn't more than 0");
    CHECK_EQ(graph(R"("field": "power", "line": { "colour": "#fff" })"), "element 1: \"colour\" isn't a property of \"line\"");
    CHECK_EQ(graph(R"("field": "power", "cursor": { "radius": -1 })"), "element 1: \"cursor.radius\" isn't more than 0");
    CHECK_EQ(graph(R"("field": "power", "shape": "arc")"), "element 1: \"shape\" isn't a property of a graph");
}

// Every element has a panel: none unless it says, and from a style too.
void test_panels()
{
    auto const layout = decode(R"({
        "styles": { "panel": { "background": "#00000060", "radius": 16, "padding": 12,
                               "border": { "color": "#ffffff40", "width": 1 } } },
        "elements": [
            { "type": "text", "text": "a", "style": "panel" },
            { "type": "value", "field": "power", "style": "panel" },
            { "type": "gauge", "field": "power", "style": "panel", "size": { "width": 100, "height": 20 } },
            { "type": "graph", "field": "power", "padding": 4, "size": { "width": 100, "height": 20 } },
            { "type": "text", "text": "b" }
        ]
    })");
    CHECK(layout.has_value());
    if (!layout || layout->elements.size() != 5) {
        return;
    }
    auto const panel_of = [](OverlayElement const &e) { return std::visit([](auto const &element) { return element.panel; }, e); };
    for (auto i = 0uz; i < 3; ++i) {
        auto const panel = panel_of(layout->elements[i]);
        CHECK(panel.drawn());
        CHECK_EQ(show(panel.background.value_or(BLANK)), "0 0 0 96");
        CHECK_EQ(panel.radius, 16.0f);
        CHECK_EQ(panel.padding, 12.0f);
        CHECK_EQ(show(panel.border.value_or(Stroke { }).color), "255 255 255 64");
        CHECK_EQ(panel.border.value_or(Stroke { }).width, 1.0f);
    }
    // Padding alone draws nothing, but still insets.
    auto const graph = panel_of(layout->elements[3]);
    CHECK(!graph.drawn());
    CHECK_EQ(graph.padding, 4.0f);
    CHECK(!panel_of(layout->elements[4]).drawn());

    CHECK_EQ(error_of(with_element(R"({ "type": "text", "text": "a", "radius": -2 })")), "element 1: \"radius\" is less than 0");
    CHECK_EQ(error_of(with_element(R"({ "type": "text", "text": "a", "padding": "lots" })")), "element 1: \"padding\" isn't a number");
    CHECK_EQ(error_of(with_element(R"({ "type": "text", "text": "a", "border": { "width": -1 } })")),
        "element 1: \"border.width\" isn't more than 0");
}

// Gauges and graphs have no text, so no fonts.
void test_element_fonts()
{
    auto const layout = decode(R"({ "elements": [
        { "type": "text", "text": "a", "font": "A.ttf" },
        { "type": "value", "field": "power", "font": "B.ttf", "unit_style": { "font": "C.ttf" } },
        { "type": "gauge", "field": "power", "size": { "width": 100, "height": 20 } }
    ] })");
    CHECK(layout.has_value());
    if (!layout || layout->elements.size() != 3) {
        return;
    }
    CHECK_EQ(element_fonts(layout->elements[0]), std::vector<std::string> { "A.ttf" });
    CHECK_EQ(element_fonts(layout->elements[1]), (std::vector<std::string> { "B.ttf", "C.ttf" }));
    CHECK(element_fonts(layout->elements[2]).empty());
}

int main()
{
    test_cascade();
    test_styles();
    test_layout_properties();
    test_text();
    test_value();
    test_later_elements();
    test_errors();
    test_place();
    test_element_numbers();
    test_place_in_box();
    test_error_index_counts_skipped();
    test_typos_in_defaults_and_styles();
    test_cascade_in_layout();
    test_unit_style_shadow_merges();
    test_coordinates();
    test_place_scaled();
    test_value_formats();
    test_sizes();
    test_codepoints();
    test_codepoints_of_units();
    test_grade_window();
    test_color_by();
    test_color_by_errors();
    test_threshold_color();
    test_shown_value();
    test_thresholds_in_fahrenheit();
    test_threshold_at_the_shown_number();
    test_shown_grade_window();
    test_value_formats_per_kind();
    test_gauge();
    test_graph();
    test_gauge_graph_errors();
    test_panels();
    test_element_fonts();
    return ST::Test::test_result();
}
