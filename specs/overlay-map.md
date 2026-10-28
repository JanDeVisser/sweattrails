# Overlay map: the track

Status: Done

## Goal

The overlay can show where the ride is: the GPS track, with the part
ridden in one color and the rest in another, and a marker at the rider's
position, either over the whole ride or following the rider. Viewers of a
ride video want to know where it is; a profile says how far up, a map says
where.

This is slice 6 of the [overlay roadmap](overlay-layout.md#roadmap), after
[overlay-profile.md](overlay-profile.md). The format is in the layout
reference, under
[`map`](overlay-layout.md#map-gps-track-position-optional-osm-underlay).
This spec says what of it is built now, and where it departs from the
reference.

The slice is split in two. This spec, 6a, is the track: it needs no
network. The OpenStreetMap tiles under it are slice 6b, a spec of its
own, since they bring all the rest: OSM's usage policy and attribution,
the tile code's prerequisites (a URL template, a cache keyed by source,
a User-Agent with a contact, curl's verbose logging off), filters, and
fetching every tile a render needs before it starts, since a frame
can't wait for a download. A track on a dark translucent panel is a
common look for ride videos on its own.

## Feature

```json
{ "type": "map",
  "at": { "x": -40, "y": 40, "anchor": "top-right" },
  "size": { "width": 420, "height": 420 },
  "clip": "circle",
  "background": "#00000060",
  "border": { "color": "#ffffff", "width": 4 },
  "view": "follow",
  "zoom": 15,
  "track": {
    "done":      { "color": "#ffb703", "width": 6 },
    "remaining": { "color": "#ffffff80", "width": 4 },
    "outline":   { "color": "#00000080", "width": 2 }
  },
  "marker": { "color": "#ffffff", "radius": 10, "outline": "#ffb703" } }
```

What's in the reference and builds on what the app has:

- The track is the records' positions, in Web Mercator as the app's map
  has them (`world_pixel()` in `Map.h`).
- **`view`**:
  - `fit`: the whole track in the box, still, with only the marker
    moving. The scale is continuous, so the track fills the box but for a
    margin of 10%, rather than stepping by OSM's zoom levels, which double
    at each step and would leave the track anywhere from half the box to
    all of it. Slice 6b picks the tile zoom just above the scale and draws
    the tiles a little smaller, which keeps them sharp.
  - `follow`: centred on the rider at a fixed `zoom`. The zoom is OSM's
    level, so that tiles slot in later without changing layouts: 15 is
    about 4.8 m a pixel at the equator, less away from it, as on the map.
    It scales with the layout, as everything does: at 4K the box is twice
    as big and shows the same area.
- **`clip`**: `rect`, `rounded` (with the panel's `radius`) or `circle`.
  The panel takes the clip's shape: a circle map's background and border
  are a circle, rather than a square showing around it. The reference's
  `border` on the map is the panel's `border`.
- **`track`**: `done` and `remaining` strokes before and after the
  moment, which is where the marker is, and an `outline` drawn around
  both: a stroke under the track, wider by its width on each side, to
  keep the track visible over any footage.
- **`marker`**: a dot at the position, gliding between records, with an
  `outline` ring as the profile's marker has. `"marker": null` turns it
  off. In `follow`, it's at the middle of the box.
- **Records without a position**, such as in a tunnel or after the GPS
  dropped out: the track goes straight across the gap, as the app's map
  draws it, which is roughly what the ride did; a break would read as a
  jump. The marker holds at the last position and glides on from the
  next.
- An activity without positions, such as an indoor ride, draws its panel
  only, as a profile without distances does, rather than being hidden as
  the reference has it.
- A track whose positions are all the same, such as on a trainer with
  the GPS on, is a dot in the middle: `fit` zooms in no further than
  OSM's level 18, street level.
- A panel, as for every element. The clip's edge is smooth in a render,
  which draws at twice the size; in the preview it can be jagged, which
  a border, drawn over the edge, hides.

### Defaults

What a map is without saying:

- `view: "fit"`, the simplest, a still map; `zoom: 15` for `follow`,
  street level while still showing a few hundred meters around the rider;
  `clip: "rect"`.
- `track.done` in the element's `color`, 4 px; `track.remaining` in the
  same color at a third of its alpha, so that where the ride goes shows
  without competing with it; no `outline`.
- A marker, as on the profile, since a map without one hardly says where
  the rider is: a dot of radius 8 in the done track's color, without an
  outline.

### Errors

A map needs a `size`. `view` is `fit` or `follow`, `zoom` a whole number
from 1 to 18, and `clip` `rect`, `rounded` or `circle`; strokes have a
width more than 0, and the marker a radius more than 0, as elsewhere.

### Version 1 scope

In: `map` with `view` `fit` and `follow`, `zoom`, `clip`, `track` with
`done`, `remaining` and `outline`, `marker`, and the panel in the clip's
shape.

Also a map in the built-in **`cycling-dashboard`**, in its empty top
right corner: a circle, 300×300, `follow` at zoom 15, on a dark
translucent panel with a thin border, the done track in the profile's
amber so that the two read as one design, the remaining track faint
white, with a dark outline, and a white marker with an amber outline.
Until 6b, it's the track on the panel.

*(Deferred)*:

- To slice 6b: `tiles` and `attribution`, which a layout can have now,
  and which are taken and not drawn yet.
- `rotate: "heading"`, as in the reference, which a layout can say as
  `rotate` is taken and not done.
- Start and finish flags, and a marker that's an arrow along the
  direction of travel: a heading that doesn't flicker as GPS jitters at
  walking speed is real work.
- A `fit` of the lap, or of the rendered range, as for the profile.

## Open questions

None.

## Architecture

The map is decoded in `overlay/Layout`, the track and the moment's
position are in `overlay/Fields`' `OverlaySource`, the geometry in a new
`overlay/TrackMap`, and `overlay/Renderer` draws it, clipped by a small
shader.

### Clipping: a shader, not a render texture

The reference draws a map into a render texture of its own, and that
through a shader that discards what's outside the shape. But a frame is
already drawn into a render texture, the doubled one, and raylib's
texture mode doesn't nest: `EndTextureMode()` goes back to the window,
not to the frame. So instead:

- **A clip shader**: its vertex shader passes each vertex's position, in
  the coordinates everything is drawn in, to its fragment shader, which
  works out how far the pixel is inside the shape (a signed distance:
  rectangle, rounded rectangle, circle) and fades the color's alpha out
  over the last pixel. Everything drawn between `BeginShaderMode()` and
  `EndShaderMode()` is clipped: lines, circles, and in 6b the tiles.
- It doesn't care whether it draws into the window or a texture, which
  way up that is, or how big its pixels are, since it works in drawing
  coordinates. And the fade gives a smooth edge in the preview too, not
  only in a render.
- The shader's source is a string in the code, loaded once with
  `LoadShaderFromMemory()` when a map is first drawn, as it needs the GL
  context: no file in `share/` to install or miss.
- A `rect` clip uses the same shader, which keeps one way of drawing a
  map.
- It's **`ClipShader`** in the Widget library (`src/Lib/Widget`), with
  `begin(box, radius)` and `end()`, since nothing in it is the overlay's.
  A circle is a rounded rectangle with half its side for a radius, so the
  shader has one shape. The shader is checked once, after loading: where
  it doesn't compile, `ClipShader` logs it and clips to the box with
  scissor mode.

### The layout

- **`MapElement`**: `at`, `panel`, `view` (`Fit`, `Follow`), `zoom` (1 to
  18), `clip` (`Rect`, `Rounded`, `Circle`), `track` (`done`,
  `remaining`: strokes; `outline`: an optional stroke) and `marker`
  (optional).
- The profile's `marker()` decoder is shared. It decodes over the
  element's own default marker (a profile's radius 6, a map's 8), so that
  a `marker` in the defaults doesn't change a map's radius to a
  profile's. A map's marker ignores a `drop_line` from the defaults or a
  style, which can be for the profiles; one the map gives itself is an
  error.
- The remaining track's default is the done one's color, at a third of
  its alpha, and its width, so that a layout that colors only the done
  track gets a faint track of the same color. An outline without a color
  is black at half its alpha.
- `map` leaves `LaterTypes`; `view`, `zoom`, `clip`, `track`, `tiles`,
  `attribution` and `rotate` leave `LaterProperties`. `tiles`,
  `attribution` and `rotate` are taken and ignored.
- A map takes `color` but no other text style, and has no fonts.
- Defaults and errors as in the Feature.

### The track: `OverlaySource`

- **`OverlaySource::track`**: the records with a position, each as its
  time since the start and its Web Mercator point at zoom 0
  (`world_pixel(position, 0)`), worked out once. A position within 0.1°
  of 0° in both latitude and longitude is none, which is what devices
  record without a fix: `Coordinates::has_fix()`, which the app's map
  uses as well.
- **`position_at(source, seconds)`**: between the two track points around
  the moment, as `values_at_time()` interpolates the fields; held at the
  earlier one across a gap of more than `MaxGap`, so the marker holds in
  a tunnel and in a pause; the first point before the first, the last
  after the last. None without a track.

### Geometry: `overlay/TrackMap.h`

No raylib drawing, so that tests reach it:

- **`MapTransform { world center, scale, screen center }`**, with
  `to_screen(world point)`: pixels per zoom-0 world pixel.
- **`fit_transform(track, content, layout scale)`**: the track's bounds
  with 10% to spare, at the scale where they fit, but no more than zoom
  18's at the layout's scale.
- **`follow_transform(position, zoom, layout scale, content)`**: centred
  on the position, at `2^zoom` times the layout's scale.
- **`split_track(source, seconds)`**: the track's points before the
  moment and after, both with the position at the moment.
- **`screen_points(points, transform)`**: in screen pixels, leaving out a
  point less than a pixel from the one before it, which a long ride in a
  small `fit` map has thousands of.
- **`ClipShape { Rectangle box, radius }`**, from `clip_shape(kind,
  box, corner)`: a circle as big as fits in the box's middle, a rounded
  rectangle with the corner as given, at most half the shorter side, or
  the box. `map_shapes(kind, box, radius, padding)` returns both: the
  panel's shape for the box, and the track's for the content box, with
  the panel's radius less its padding.

### Drawing

- The panel takes the clip's shape. Every shape is a rounded rectangle,
  drawn with `DrawRectangleRounded()` and `DrawRectangleRoundedLinesEx()`
  as other panels are: at roundness 1 for a circle, the radius as is for
  `rounded`, and none for `rect`.
- Then, in the clip shader: the outlines under the remaining and done
  traces, wider by the outline's width on each side, the traces, and the
  marker with its outline ring.
- The border is drawn last, over the clip's edge. `draw_panel()` is
  drawing the background and drawing the border, in a `ClipShape`, which
  the map draws apart.
- Measured with the dashboard at 1080p: 300 frames took 4.0 s with the
  map and 3.8 s without, so under 1 ms a frame.

### Built-in layout

- `cycling-dashboard` gets the map in its top right corner, as in the
  Version 1 scope.

## Test plan

- `OverlayLayoutTest`: decoding maps (views, zoom, clips, track strokes
  and their defaults, the marker and `"marker": null`), the errors, a
  `drop_line` on a map's marker being one, and `tiles`, `attribution` and
  `rotate` being taken.
- `OverlayFieldsTest`: the track (positions near 0° left out), and
  `position_at()` between records, across a gap, before and after.
- `OverlayTrackMapTest` (new): `fit_transform()` for a wide and a tall
  track and a track of one point, `follow_transform()` at the layout's
  scale, `split_track()` in the middle, before the first point and after
  the last, `screen_points()` thinning, `clip_shape()` for the three shapes,
  and `map_shapes()` with padding.
- `LayoutFileTest`: `cycling-dashboard` decodes with its map.
- By hand, and with a throwaway frame dump: the dashboard's map in the
  preview and a render, the circle's edge in both, `follow` and `fit`,
  and the render's speed.

## Review notes

1. **Bug**: `fit` cuts off the track in a `circle` clip, and can in a
   `rounded` one. `fit_transform()` (`src/App/overlay/TrackMap.cpp:572`)
   fits the track's bounds into the clip's box, and `Renderer.cpp:463`
   passes it `inner.box`, the circle's bounding square. The corners of
   the bounds are then at 0.64 of the side from the middle, outside the
   circle's radius of 0.5. So a ride whose start or end is in a corner of
   its bounds (a diagonal or an L-shaped route) loses those ends, and the
   marker vanishes there. With `rounded`, the corners cut in by about
   0.29 of the radius, more than the 10% margin (4.5% of the side) once
   the radius is over about 0.15 of the side: say 60 on a 300 box.
   `test_fit_in_a_circle` in `OverlayTrackMapTest` fails on this. Fix:
   give `fit_transform()` the `ClipShape`, and for a circle fit the
   bounds' diagonal, `hypot(width, height)`, into the diameter; for a
   rounded clip, take the corner's cut into account, or simply fit the
   diagonal there too when the radius is large.
2. **Suggestion**: a position within 0.1° of 0° on *one* axis counts as
   no fix (`src/App/overlay/Fields.cpp:387`, as in
   `widget/ActivityMap.cpp:69`). That drops every position in a strip
   about 14 km wide along the prime meridian (Greenwich, east London,
   Le Havre, Castellón) and 22 km wide along the equator: there the
   track goes straight across and the marker holds. It's what the spec
   says, but a device without a fix records both near 0, so the check
   could be that both are, in both places, in a spec of its own.
   `test_track_gap_and_near_zero` pins down today's behavior and would
   change with it.
3. **Suggestion**: `ClipShader::begin()` (`src/Lib/Widget/ClipShader.cpp:796`)
   doesn't check that the shader loaded. If it doesn't compile (it's
   hard-coded `#version 330`, so any context other than GL 3.3 core),
   raylib hands back its default shader, and the map draws unclipped
   without a message: in `follow`, the track spreads over the whole
   frame. Check `IsShaderValid()` once after loading, log it, and fall
   back to scissor mode or leave the track out.
4. **Suggestion**: the panel and clip shapes for the padding are worked
   out in the renderer (`Renderer.cpp:453-455`: the inner radius is the
   panel's less the padding), so the Test plan's "`clip_shape()` for the
   three shapes with padding" isn't tested, as it can't be there. A
   `map_shapes(kind, box, radius, padding)` in `TrackMap` returning the
   outer and inner `ClipShape`s would let a test reach it.
5. **Suggestion**: the Architecture's Drawing section says a circle panel
   is drawn with `DrawCircleV()` and `DrawRing()`; the code draws every
   shape with `DrawRectangleRounded()` and `DrawRectangleRoundedLinesEx()`,
   at roundness 1 for a circle. Update the spec to what was built.
6. **Missing tests**, now added: the cycling-dashboard's map as the
   Version 1 scope has it (`LayoutFileTest`: a circle, 300x300, follow at
   15, the done track and marker outline in the profile's amber, an
   outline, a white marker); map properties cascading from the defaults
   and a style, with the track's strokes merging, a marker radius staying
   8 for a map and 6 for a profile, a style's `drop_line` ignored, an
   outline of `{ }` being black at half alpha, and `fit`, `rect` and
   `rounded` by name (`OverlayLayoutTest`); a gap of exactly `MaxGap`
   glided across and one over it held, and a position near 0° on only
   one axis left out (`OverlayFieldsTest`); `split_track()` at exactly a
   record and `fit_transform()` in a content box away from the origin
   (`OverlayTrackMapTest`).

### Resolution

1. Fixed: `fit_transform()` takes the `ClipShape`. In a circle it fits
   the bounds' diagonal into the diameter; in a rounded rectangle, the
   sides less what a corner cuts in where the bounds' corner would be.
   `test_fit_in_a_rounded_rectangle` added.
2. Fixed, in this spec rather than one of its own, as it's one line in
   each place: `Coordinates::has_fix()` is false only with both
   coordinates near 0°, in the overlay and in the app's map.
   `test_track_gap_and_near_zero` now has a point at Greenwich and one on
   the equator.
3. Fixed: the shader is checked with `IsShaderValid()` after loading;
   where it's not valid, it's logged once and the map clips to the box
   with scissor mode.
4. Fixed: `map_shapes()` in `TrackMap`, with `test_map_shapes`.
5. Fixed: the Drawing section says what was built.
6. Committed.

The GLSL reads right: it's the usual rounded-box signed distance, the
attribute and uniform names are raylib's defaults, the uniforms are set
before `BeginShaderMode()` flushes the batch, so two maps in a frame each
get their own box, and nothing in the app moves the drawing with
`rlPushMatrix()`, which would put `vertexPosition` in other coordinates
than the box.
