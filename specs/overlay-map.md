# Overlay map: the track

Status: Ready

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

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
