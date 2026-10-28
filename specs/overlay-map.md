# Overlay map: the track

Status: In progress

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

### The layout

- **`MapElement`**: `at`, `panel`, `view` (`Fit`, `Follow`), `zoom` (1 to
  18), `clip` (`Rect`, `Rounded`, `Circle`), `track` (`done`,
  `remaining`: strokes; `outline`: an optional stroke) and `marker`
  (optional).
- The profile's `marker()` decoder is shared, with what each element
  takes: a map's marker has no `drop_line`.
- `map` leaves `LaterTypes`; `view`, `zoom`, `clip`, `track`, `tiles`,
  `attribution` and `rotate` leave `LaterProperties`. `tiles`,
  `attribution` and `rotate` are taken and ignored.
- A map takes `color` but no other text style, and has no fonts.
- Defaults and errors as in the Feature.

### The track: `OverlaySource`

- **`OverlaySource::track`**: the records with a position, each as its
  time since the start and its Web Mercator point at zoom 0
  (`world_pixel(position, 0)`), worked out once. A position within 0.1°
  of 0° latitude or longitude is none, as in the app's map, which is what
  devices record without a fix.
- **`position_at(source, seconds)`**: between the two track points around
  the moment, as `values_at_time()` interpolates the fields; held at the
  earlier one across a gap of more than `MaxGap`, so the marker holds in
  a tunnel and in a pause; the first point before the first, the last
  after the last. None without a track.

### Geometry: `overlay/TrackMap.h`

No raylib drawing, so that tests reach it:

- **`MapTransform { world center, scale, screen center }`**, with
  `to_screen(world point)`: pixels per zoom-0 world pixel.
- **`fit_transform(track, content)`**: the track's bounds with 10% to
  spare, at the scale where they fit, but no more than zoom 18's.
- **`follow_transform(position, zoom, layout scale, content)`**: centred
  on the position, at `2^zoom` times the layout's scale.
- **`split_track(source, seconds)`**: the track's points before the
  moment and after, both with the position at the moment.
- **`screen_points(points, transform)`**: in screen pixels, leaving out a
  point less than a pixel from the one before it, which a long ride in a
  small `fit` map has thousands of.
- **`ClipShape { kind, Rectangle box, radius }`** for the content box:
  a circle as big as fits in its middle, a rounded rectangle with the
  panel's radius less its padding, or the box.

### Drawing

- The panel takes the clip's shape: `DrawCircleV()` and `DrawRing()` for
  a circle; the rounded rectangle as other panels have it, with the
  radius as is for `rounded`, and none for `rect`.
- Then, in the clip shader: the outlines under the remaining and done
  traces, wider by the outline's width on each side, the traces, and the
  marker with its outline ring.
- The border is drawn last, over the clip's edge.

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
  the last, `screen_points()` thinning, and `clip_shape()` for the three
  shapes with padding.
- `LayoutFileTest`: `cycling-dashboard` decodes with its map.
- By hand, and with a throwaway frame dump: the dashboard's map in the
  preview and a render, the circle's edge in both, `follow` and `fit`,
  and the render's speed.

## Review notes

*Filled in by the Reviewer.*
