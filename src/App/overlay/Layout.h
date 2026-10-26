/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * An overlay layout, decoded from its JSON: what's shown where, in which
 * font, size and color. The format is in specs/overlay-layout.md; this
 * slice decodes text and value elements (specs/overlay-preview.md).
 */

#pragma once

#include <expected>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include <raylib.h>

#include <JSON.h>

#include <overlay/Fields.h>
#include <overlay/Template.h>

namespace ST {

enum class Anchor {
    TopLeft,
    Top,
    TopRight,
    Left,
    Center,
    Right,
    BottomLeft,
    Bottom,
    BottomRight,
};

enum class Align {
    Left,
    Center,
    Right,
};

// Design pixels, or a percentage of the canvas. Negative from the right or
// bottom edge.
struct Coordinate {
    double value { 0 };
    bool   percent { false };
};

struct Placement {
    Coordinate             x { };
    Coordinate             y { };
    Anchor                 anchor { Anchor::TopLeft };
    std::optional<Vector2> size { }; // The box, in design pixels; else the text's
};

struct Shadow {
    Color   color { BLACK };
    Vector2 offset { }; // In design pixels
};

struct TextStyle {
    std::string           font { "Inter-Regular.ttf" };
    float                 font_size { 48 }; // In design pixels
    Color                 color { WHITE };
    std::optional<Shadow> shadow { };
};

struct TextElement {
    Placement at { };
    TextStyle style { };
    Template  text { };
    Align     align { Align::Left };
};

struct ValueElement {
    Placement                at { };
    TextStyle                style { };
    TextStyle                unit_style { };
    OverlayField             field { OverlayField::speed };
    std::string              spec { }; // The default spec if empty
    std::optional<FieldUnit> unit { }; // The layout's units if none
    std::string              missing { "--" };
    bool                     hide_when_missing { false };
};

using OverlayElement = std::variant<TextElement, ValueElement>;

struct OverlayLayout {
    Vector2                     design_size { .x = 1920, .y = 1080 };
    Units                       units { Units::Metric };
    std::vector<OverlayElement> elements { };
    size_t                      skipped { 0 }; // Elements of later slices
};

// What's wrong with a layout: the element, counted from 1, if it's in one.
struct LayoutError {
    std::optional<size_t> element { };
    std::string           message { };

    [[nodiscard]] std::string to_string() const;
};

std::expected<OverlayLayout, LayoutError> decode_layout(JSONValue const &json);

// The style cascade: `over`'s properties replace `base`'s, and where both
// are objects, they merge. Arrays are replaced, unlike JSONValue::merge().
JSONValue cascade(JSONValue base, JSONValue const &over);

// How much the layout is scaled to fit the canvas.
float layout_scale(Vector2 design_size, Vector2 canvas);

// The element's box on the canvas, given its size there, in canvas pixels.
Rectangle place(Placement const &at, Vector2 size, Vector2 canvas, float scale);

// The characters the layout draws besides ASCII, such as `·`, for loading
// the fonts with.
std::set<int> layout_codepoints(OverlayLayout const &layout);

}
