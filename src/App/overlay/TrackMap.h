/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The overlay's map: where the track goes in the box, for the whole ride
 * or following the rider, the track split at the moment, and the shape
 * the map is clipped to. No drawing, so that tests reach all of it. In
 * pixels, once the renderer has scaled the layout. See
 * specs/overlay-map.md.
 */

#pragma once

#include <optional>
#include <vector>

#include <raylib.h>

#include <Map.h>
#include <overlay/Fields.h>
#include <overlay/Layout.h>

namespace ST {

// Web Mercator at zoom 0 to the screen: `center` is at `screen_center`, and
// a world pixel is `scale` screen pixels.
struct MapTransform {
    WorldPoint center { };
    double     scale { 1 };
    Vector2    screen_center { };

    [[nodiscard]] Vector2 to_screen(WorldPoint const &point) const;
};

// What the map is clipped to: a rounded rectangle, which with no radius is
// a rectangle, and with half its side a circle.
struct ClipShape {
    Rectangle box { };
    float     radius { 0 };
};

// In the content: a circle as big as fits, in its middle; the content with
// rounded corners of `corner`; or the content.
ClipShape clip_shape(ClipKind kind, Rectangle content, float corner);

// The whole track in the clip, with 10% to spare, but zoomed in no further
// than OSM's level 18, at the layout's scale: in a circle, the corners of
// the track's bounds inside it, and in a rounded rectangle, clear of its
// corners. None without a track.
std::optional<MapTransform> fit_transform(std::vector<OverlaySource::TrackPoint> const &track, ClipShape const &clip, float layout_scale);

// Centred on the position, at OSM's `zoom` level, at the layout's scale.
MapTransform follow_transform(WorldPoint position, int zoom, float layout_scale, Rectangle content);

// The track before the moment and after it, both with the position at the
// moment.
struct SplitTrack {
    std::vector<WorldPoint> done { };
    std::vector<WorldPoint> remaining { };
};

SplitTrack split_track(OverlaySource const &source, double seconds);

// On the screen, leaving out a point less than a pixel from the one before
// it; the last is always there.
std::vector<Vector2> screen_points(std::vector<WorldPoint> const &points, MapTransform const &transform);

}
