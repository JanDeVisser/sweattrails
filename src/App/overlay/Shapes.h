/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The geometry of the overlay's gauges, graphs and panels, and the values a
 * graph shows: no drawing, so that tests reach all of it. In pixels, once
 * the renderer has scaled the layout. See specs/overlay-gauge-graph.md.
 */

#pragma once

#include <optional>
#include <vector>

#include <raylib.h>

#include <overlay/Fields.h>
#include <overlay/Layout.h>

namespace ST {

// An arc without a thickness is a fifth of its radius thick.
constexpr float DefaultArcThickness = 0.2f;

// The arc's 270°, from the bottom left, clockwise.
constexpr float ArcStart = 135.0f;
constexpr float ArcSweep = 270.0f;

// The box inset by its padding on every side; never less than nothing.
Rectangle content_box(Rectangle box, float padding);

// The size of an element's box, in pixels at `scale`: its own size, or
// else its content's, measured, with the padding around it.
Vector2 box_size(Placement const &at, Vector2 content, Panel const &panel, float scale);

// An image's content, in design pixels: the picture's own size, a pixel
// to a design pixel; the side given, and the other in the picture's
// aspect ratio; or both as given.
Vector2 image_size(Vector2 picture, std::optional<float> width, std::optional<float> height);

// The picture as big as fits in `content` without stretching, where
// `anchor` is in it.
Rectangle fit_picture(Rectangle content, Vector2 picture, Anchor anchor);

// The lowest and the highest value a gauge or a graph shows, in the unit
// shown: the element's own, or the activity's range for "auto".
struct Bounds {
    double min { 0 };
    double max { 1 };
};

// None if "auto" has no range: the activity hasn't got the field. Bounds
// that meet, such as a field that never changes, are 1 apart.
std::optional<Bounds> shown_bounds(OverlaySource const &source, OverlayField field, FieldUnit const &unit,
    std::optional<double> min, std::optional<double> max);

// How far the gauge is filled, from 0 to 1: a value past an end fills to
// that end. None when the value is missing.
std::optional<float> gauge_fraction(std::optional<double> shown, Bounds const &bounds);

struct BarRects {
    Rectangle track { };
    Rectangle fill { };
};

// A bar spans the content's width, `thickness` high and centred, or the
// content's height without one, and fills from the left. A vbar is the
// same, upright, filling from the bottom.
BarRects bar_rects(Rectangle content, GaugeShape shape, std::optional<float> thickness, float fraction);

// The arc's ring in the content: centred, as big as fits.
struct Ring {
    Vector2 center { };
    float   inner { 0 };
    float   outer { 0 };
};

Ring arc_ring(Rectangle content, std::optional<float> thickness);

// In raylib's degrees: 0 is to the right, and they go clockwise on screen.
struct ArcAngles {
    float start { ArcStart };
    float end { ArcStart };
};

ArcAngles arc_angles(float fraction);

// Where the graph is at the frame: its elapsed time, or its distance on a
// distance axis. None without a distance.
std::optional<double> graph_now(GraphElement const &graph, FieldValues const &values);

// One value per pixel column, in the unit shown, or none for a break in
// the line. The columns share out the window ending at `now`, or the whole
// activity for "all". A column's value is the average of the records in
// its stretch, or the value at its middle when it has none, as in a pause.
// Before the start and after the end there's nothing.
std::vector<std::optional<double>> graph_columns(OverlaySource const &source, GraphElement const &graph,
    FieldUnit const &unit, double now, size_t columns);

// Where the moment is along the graph, from 0 to 1: at the right edge of
// a rolling window, and moving along with "all". None without records.
std::optional<float> graph_position(OverlaySource const &source, GraphElement const &graph, double now);

// The height of a value in the content, clamped to it: `min` at the
// bottom, `max` at the top.
float graph_y(double shown, Bounds const &bounds, Rectangle content);

}
