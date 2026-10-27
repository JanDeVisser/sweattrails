# Overlay elevation profile

Status: Draft

## Goal

The overlay can show the ride's elevation profile with a marker where the
rider is: how far up the climb they are, what's still to come, and how the
footage fits into the whole ride. Numbers and a graph of the last minute
don't say that; a profile does at a glance, which is why it's on almost
every cycling broadcast.

This is slice 5 of the [overlay roadmap](overlay-layout.md#roadmap), after
[overlay-gauge-graph.md](overlay-gauge-graph.md). The format is in the
layout reference, under
[`profile`](overlay-layout.md#profile-elevation-profile-with-the-current-position).
This spec says what of it is built now, and where it departs from the
reference.

## Feature

```json
{ "type": "profile",
  "at": { "x": "50%", "y": -40, "anchor": "bottom" },
  "size": { "width": 900, "height": 140 },
  "extent": "all", "smooth": 20,
  "min": "auto", "max": "auto", "min_span": 100,
  "done":      { "line": { "color": "#ffb703", "width": 3 }, "fill": "#ffb70360" },
  "remaining": { "line": { "color": "#ffffff", "width": 3 }, "fill": "#ffffff30" },
  "marker": { "color": "#ffffff", "radius": 8, "outline": "#ffb703", "drop_line": true },
  "font": "Inter-Regular.ttf", "font_size": 22,
  "marker_label": { "text": "{altitude:.0f} {altitude.unit} · {grade:+.1f}%", "style": "label" },
  "axis_labels": { "min_max": true, "style": "label" },
  "laps": { "color": "#ffffff60", "width": 1 } }
```

- **`extent`**: which part of the ride the profile covers.
  - `"all"`, the default: the whole activity. The marker moves from left
    to right over the render.
  - `"lap"`: the current lap. The profile changes at each new lap, which
    suits rides split into climbs. An activity without laps is one lap.
  - *(deferred)* A fixed `{ "from": 12.5, "to": 31.0 }` in km, as in the
    reference. A section of one ride doesn't belong in a layout, which is
    reused for every ride. What's wanted is rather "the part being
    rendered", the Ctrl+E range, for a video of one climb; but the preview
    has no range. That extent is an idea for later.
  - *(deferred)* `{ "behind", "ahead" }`, a window scrolling past a fixed
    marker, as in the reference.
- **`smooth`**: the altitude is averaged over this many meters, centred on
  each point, 20 by default; 0 for none. GPS altitude especially is
  jagged, which shows on a short lap, where a pixel column is a couple of
  meters. In meters rather than the reference's seconds: on a distance
  axis, seconds would smooth a slow climb over a shorter stretch than a
  fast descent. On top of it, each pixel column is the average of its
  stretch, as in a graph, which is what smooths a whole ride: 100 km in
  900 px is about 110 m a column.
- **`min` and `max`**: a number, in the unit shown, or `"auto"`, the
  default: the lowest and highest altitude in the extent, so a `"lap"`
  profile is scaled to its lap. Directly on the element, as for gauges
  and graphs, rather than in the reference's `scale` object, which this
  spec replaces.
- **`min_span`**: the least an `"auto"` scale spans, in the unit shown, 100
  by default (m, or ft for an imperial layout). It keeps a flat ride from
  being stretched until 5 m of rolling road looks like the Alps; the
  profile then sits at the bottom of its span. A `min` and a `max` that
  are both numbers are as given.
- **`axis_labels`**: `min_max` writes the lowest and highest altitude of
  the scale at the left edge, inside the box, in whole units with the
  unit, such as `820 m` at the bottom and `1240 m` at the top. Distance ticks along the bottom
  are *(deferred)*: spacing them so they read well at any length is real
  work, and they take height from a short box.
- The elevation against distance, from the source's profile: the records
  with both, with an altimeter's drift during a stop taken out, as for the
  grade ([overlay-fields.md](overlay-fields.md)). A pause takes no width.
- **`done` and `remaining`**: line and fill before and after the moment.
  Without `remaining`, `done` is the whole profile.
- **`marker`**: a dot on the line at the moment, at the frame's distance,
  so that it glides between records as every field does, and holds in a
  pause. Before the first distance there's no marker, and all of the
  profile is `remaining`.
  - `outline`: a ring 2 design pixels wide around the dot, in that color.
  - `drop_line`: `true` for a 1 px line from the dot down to the bottom
    of the box, in the marker's color at half its alpha; or a stroke,
    `{ "color", "width" }`, for another look.
- **`marker_label`**: a `text` template that moves with the marker, above
  and to the right of it, in the corner that keeps the line ahead clear.
  Near the right edge it flips to the left of the marker, and on a summit
  with no room above, it goes below. It stays inside the element's box.
  Missing fields show as `--`, as in a `text`.
- **Text style**: `marker_label` and `axis_labels` take the text style
  properties, a named `style` included, over the profile's own, which it
  takes as any element does: from the defaults, its styles, and its own
  `font`, `font_size`, `color` and `shadow`.
- **`laps`**: a stroke for thin vertical lines where laps start. None
  without it, and none with `"lap"`, which shows one lap.
- A panel behind it, as for every element.
- An activity without distances, such as an indoor ride, draws the panel
  only, as a graph on a distance axis does.

### Version 1 scope

In: `profile` with `extent` `"all"` and `"lap"`, `done` and `remaining`,
`marker`, `marker_label`, `smooth`, `min`, `max` and `min_span`,
`axis_labels.min_max`, `laps` and a panel.

Also a profile across the top of the built-in **`cycling-dashboard`**,
centred, 900×120, over the whole ride, with the marker's label and the
altitude labels: the example to copy, and what the hand test needs.
`"lap"` is a one-word change for a ride of climbs. No new layout.

*(Deferred)*: fixed and scrolling extents, an extent for the rendered
range, distance ticks, and `color_by_grade`, as in the reference.

## Open questions

None.

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
