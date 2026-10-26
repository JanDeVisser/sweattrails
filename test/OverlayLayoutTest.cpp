/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Decoding overlay layouts, the style cascade, and placing elements.
 */

#include <format>
#include <string>

#include <JSON.h>

#include <overlay/Layout.h>

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
    CHECK_EQ(error_of(with_element(R"({ "type": "value", "field": "lap" })")), "element 1: \"field\": `lap` can't be shown as a value yet");
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
    CHECK_EQ(error_of(R"({ "elements": [ { "type": "map" }, { "type": "gauge" }, { "type": "text", "text": "a", "colour": "#fff" } ] })"),
        "element 3: \"colour\" isn't a property of a text");
}

// A typo in the defaults or a style is an error too, on the first element
// that takes it.
void test_typos_in_defaults_and_styles()
{
    CHECK(error_of(R"({ "defaults": { "colour": "#fff" }, "elements": [ { "type": "text", "text": "a" } ] })").contains("\"colour\""));
    CHECK(error_of(R"({ "styles": { "big": { "fontsize": 64 } }, "elements": [ { "type": "text", "text": "a", "style": "big" } ] })").contains("\"fontsize\""));
    CHECK_EQ(error_of(with_element(R"({ "type": "text", "text": "a", "style": ["big", 3] })")), "element 1: \"style\" isn't a string");
    CHECK_EQ(error_of(with_element(R"({ "type": "text", "text": "a", "style": 3 })")), "element 1: \"style\" isn't a name or a list of names");
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
        { "type": "text", "text": "a" }, { "type": "gauge" }, { "type": "value", "field": "speed" } ] })");
    CHECK(layout.has_value());
    if (layout && layout->elements.size() == 2) {
        CHECK_EQ(element_number(layout->elements[0]), 1uz);
        CHECK_EQ(element_number(layout->elements[1]), 3uz);
    }
}
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
    test_error_index_counts_skipped();
    test_typos_in_defaults_and_styles();
    test_cascade_in_layout();
    test_unit_style_shadow_merges();
    test_coordinates();
    test_place_scaled();
    test_value_formats();
    test_sizes();
    test_codepoints();
    return ST::Test::test_result();
}
