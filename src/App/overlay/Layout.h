/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * An overlay layout, decoded from its JSON: what's shown where, in which
 * font, size and color. The format is in specs/02-overlay-layout.md; this
 * slice decodes text and value elements (specs/12-overlay-preview.md).
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

// An element's place in the layout's elements, from 1: "3", or "3.2" for
// the second child of the third element, a group.
using ElementNumber = std::string;

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

// When an element shows, in seconds of the video: from its start, or if
// negative, back from its end. No `to` is the end.
struct Visibility {
    double                from { 0 };
    std::optional<double> to { };
    double                fade { 0 }; // In and out, inside the window
};

// Where a frame is in the video, and how long the video is, in seconds.
struct VideoTime {
    double at { 0 };
    double length { 0 };
};

// 0 when hidden, 1 when shown, and in between while it fades. An element
// without a Visibility always shows.
float visibility_alpha(std::optional<Visibility> const &visible, VideoTime time);

struct TextStyle {
    std::string           font { "Inter-Regular.ttf" };
    float                 font_size { 48 }; // In design pixels
    Color                 color { WHITE };
    std::optional<Shadow> shadow { };
};

// A line: a graph's, or a panel's border.
struct Stroke {
    Color color { WHITE };
    float width { 2 }; // In design pixels
};

// What's behind an element's box, and how far its content is in from the
// box's edges. In design pixels.
struct Panel {
    std::optional<Color>  background { };
    float                 radius { 0 };
    float                 padding { 0 };
    std::optional<Stroke> border { };

    [[nodiscard]] bool drawn() const { return background || border; }
};

struct TextElement {
    ElementNumber             number { };  // Its place in the layout's elements
    std::optional<Visibility> visible { }; // None: always
    Placement                 at { };
    Panel                     panel { };
    TextStyle                 style { };
    Template                  text { };
    std::optional<Align>      align { }; // As the anchor if none
};

// `color_by`'s: from the value up, in the unit shown, the number's color.
struct Threshold {
    double from { 0 };
    Color  color { WHITE };
};

struct ValueElement {
    ElementNumber             number { };  // Its place in the layout's elements
    std::optional<Visibility> visible { }; // None: always
    Placement                 at { };
    Panel                     panel { };
    TextStyle                 style { };
    TextStyle                 unit_style { };
    OverlayField              field { OverlayField::speed };
    std::string               spec { }; // The default spec if empty
    std::optional<FieldUnit>  unit { }; // The layout's units if none
    std::string               missing { "--" };
    bool                      hide_when_missing { false };
    std::optional<double>     window { };     // Grade's, in m; DefaultGradeWindow if none
    std::vector<Threshold>    thresholds { }; // Going up
};

enum class GaugeShape {
    Bar,  // Filling from the left
    VBar, // Filling from the bottom
    Arc,  // 270°, open at the bottom, filling clockwise
};

struct GaugeElement {
    ElementNumber             number { };  // Its place in the layout's elements
    std::optional<Visibility> visible { }; // None: always
    Placement                 at { };      // With a size: there's no text to measure
    Panel                     panel { };
    OverlayField              field { OverlayField::speed };
    std::optional<FieldUnit>  unit { }; // The layout's units if none
    GaugeShape                shape { GaugeShape::Bar };
    std::optional<double>     min { }; // In the unit shown; none is "auto"
    std::optional<double>     max { };
    Color                     track { };
    Color                     fill { WHITE };
    std::optional<float>      thickness { }; // In design pixels; the box's if none
    std::vector<Threshold>    thresholds { };
};

enum class GraphAxis {
    Time,
    Distance,
};

struct Cursor {
    Color color { WHITE };
    float radius { 6 }; // In design pixels
};

struct GraphElement {
    ElementNumber             number { };  // Its place in the layout's elements
    std::optional<Visibility> visible { }; // None: always
    Placement                 at { };      // With a size
    Panel                     panel { };
    OverlayField              field { OverlayField::speed };
    std::optional<FieldUnit>  unit { };
    GraphAxis                 axis { GraphAxis::Time };
    std::optional<double>     window { }; // Seconds or meters, ending at the moment; none is "all"
    std::optional<double>     min { };    // In the unit shown; none is "auto"
    std::optional<double>     max { };
    Stroke                    line { };
    std::optional<Color>      fill { };
    std::optional<Cursor>     cursor { };
};

enum class ProfileExtent {
    All, // The whole activity
    Lap, // The lap the moment is in
};

// A part of the profile: before the moment, or after it.
struct Trace {
    Stroke               line { };
    std::optional<Color> fill { };
};

struct Marker {
    Color                 color { WHITE };
    float                 radius { 6 };  // In design pixels
    std::optional<Color>  outline { };   // A ring around the dot
    std::optional<Stroke> drop_line { }; // Down to the bottom of the box
};

struct ProfileLabel {
    Template  text { };
    TextStyle style { };
};

struct ProfileElement {
    ElementNumber               number { };  // Its place in the layout's elements
    std::optional<Visibility>   visible { }; // None: always
    Placement                   at { };      // With a size
    Panel                       panel { };
    TextStyle                   style { }; // What its labels' styles are over
    ProfileExtent               extent { ProfileExtent::All };
    double                      smooth { 20 }; // m, centred
    std::optional<double>       min { };       // In the unit shown; none is "auto"
    std::optional<double>       max { };
    double                      min_span { 100 }; // The least an "auto" scale spans, in the unit shown
    Trace                       done { };
    std::optional<Trace>        remaining { }; // As `done` if none
    std::optional<Marker>       marker { };
    std::optional<ProfileLabel> marker_label { };
    std::optional<TextStyle>    axis_labels { }; // The scale's lowest and highest, if asked for
    std::optional<Stroke>       laps { };
};

enum class MapMode {
    Fit,    // The whole track, still
    Follow, // Centred on the rider
};

enum class ClipKind {
    Rect,
    Rounded, // With the panel's radius
    Circle,
};

struct MapTrack {
    Stroke                done { .width = 4 };
    Stroke                remaining { .width = 4 };
    std::optional<Stroke> outline { }; // Around both, this much wider on each side
};

enum class TileFilterKind {
    None,
    Grayscale,
    Dark, // Grayscale, inverted
    Tint, // Grayscale, times the tint
};

struct TileFilter {
    TileFilterKind kind { TileFilterKind::None };
    Color          tint { WHITE };
};

// OpenStreetMap under the track, which is the only source.
struct MapTileLayer {
    float      opacity { 1 };
    TileFilter filter { };
};

struct MapElement {
    ElementNumber               number { };  // Its place in the layout's elements
    std::optional<Visibility>   visible { }; // None: always
    Placement                   at { };      // With a size
    Panel                       panel { };   // In the clip's shape
    MapMode                     mode { MapMode::Fit };
    int                         zoom { 15 }; // OSM's level, for Follow
    ClipKind                    clip { ClipKind::Rect };
    MapTrack                    track { };
    std::optional<Marker>       marker { };
    std::optional<MapTileLayer> tiles { };
    TextStyle                   attribution { }; // Of the tiles, which always have it
};

// A picture: a PNG or a JPEG, found next to the layout or with the
// built-in ones, never stretched.
struct ImageElement {
    ElementNumber             number { };  // Its place in the layout's elements
    std::optional<Visibility> visible { }; // None: always
    Placement                 at { };      // Without a size: that's width and height
    Panel                     panel { };
    std::string               src { };    // As the layout says it
    std::optional<float>      width { };  // In design pixels; else the picture's, or
    std::optional<float>      height { }; // in its aspect ratio
    Color                     tint { WHITE };
};

struct GroupElement;

using OverlayElement = std::variant<TextElement, ValueElement, GaugeElement, GraphElement, ProfileElement, MapElement, ImageElement, GroupElement>;

// A panel, with its children placed in its box less its padding, drawn
// over it, and shown while it shows. Its text properties came down to the
// children in the cascade.
struct GroupElement {
    ElementNumber               number { };
    std::optional<Visibility>   visible { }; // None: always
    Placement                   at { };      // Always with a size
    Panel                       panel { };
    std::vector<OverlayElement> children { };
};

struct OverlayLayout {
    Vector2                     design_size { .x = 1920, .y = 1080 };
    Units                       units { Units::Metric };
    std::vector<OverlayElement> elements { };
};

// What's wrong with a layout: the element, counted from 1, if it's in one.
struct LayoutError {
    std::optional<ElementNumber> element { };
    std::string                  message { };

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

// Where content of `size` goes in an element's box: where the anchor is in
// the box, so that a box anchored bottom-left has it at its bottom left.
// A text's `align` picks the horizontal place instead. Relative to the
// box's top left corner.
Vector2 place_in_box(Vector2 box, Vector2 size, Anchor anchor, std::optional<Align> align = { });

// What a value element shows for the frame's values: the number, its unit,
// and the number's color, by `color_by`. Nothing if it's missing and hides.
struct ShownValue {
    std::string number { };
    std::string unit { }; // Empty when missing
    Color       color { WHITE };
};

std::optional<ShownValue> shown_value(ValueElement const &value, FieldValues const &values, Units units);

// The color of the last threshold at or below `shown`; else, or without a
// value, `own`.
Color threshold_color(std::vector<Threshold> const &thresholds, std::optional<double> shown, Color own);

// The fonts the element draws with, by the layout's names for them: none
// for a gauge or a graph.
std::vector<std::string> element_fonts(OverlayElement const &element);

ElementNumber element_number(OverlayElement const &element);

// Every element, with each group's children after it, and theirs: for
// what needs all of them, but not where they are.
std::vector<OverlayElement const *> all_elements(std::vector<OverlayElement> const &elements);

// The characters the layout draws besides ASCII, such as `·`, for loading
// the fonts with.
std::set<int> layout_codepoints(OverlayLayout const &layout);

}
