# Overlay elevation profile

Status: In progress

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

The profile is decoded in `overlay/Layout`, its geometry and values are
plain functions in a new `overlay/ElevationProfile`, next to
`overlay/Shapes`, and `overlay/Renderer` draws it. (Not
`overlay/Profile`: `fit/Profile` is the FIT profile.)

### The layout

- **`ProfileElement`**: `at`, `panel`, `style` (its own text style, the
  base of its labels'), `extent` (`All`, `Lap`), `smooth` (m, 20), `min`
  and `max` (optional: none is `"auto"`), `min_span` (100), `done` and
  `remaining` (optional), `marker` (optional), `marker_label` (optional),
  `axis_labels` (optional: none unless `min_max`), `laps` (an optional
  stroke).
- **`Trace { Stroke line, std::optional<Color> fill }`**, for `done` and
  `remaining`. Without `done`, the line is the element's `color`, 2 px,
  as a graph's is.
- **`Marker { color, radius, outline, drop_line }`**: `outline` an
  optional color; `drop_line` an optional stroke, which `true` makes from
  the marker's color at half its alpha, 1 px. The reference's
  `"shape": "dot"` is taken, and any other shape is an error. A profile
  has a marker unless it says `"marker": null`: a profile is hardly one
  without. By default it's a dot of radius 6 in the `done` line's color,
  without an outline or a drop line.
- **`ProfileLabel { Template text, TextStyle style }`**. Its style is the
  label object cascaded over its named `style` (as an element's is, with
  `cascaded()`), over the profile's own style. `axis_labels` is a
  `TextStyle` the same way.
- `profile` leaves `LaterTypes`, and its properties `LaterProperties`,
  except the deferred `color_by_grade` and `axis_labels.distance`, which
  are taken and ignored.
- `element_fonts()` gives the labels' fonts, and `layout_codepoints()`
  their templates' characters and the `--` of their missing fields.
- **Errors**: a profile needs a `size`; `extent` is `"all"` or `"lap"`
  (a fixed section says it isn't there yet); `smooth` isn't negative;
  `min_span` is more than 0; `min` below `max`; and a template that
  doesn't parse.

### Elevation: `OverlaySource`

- **`OverlaySource::profile_area`**: for each profile point, the area under
  the profile up to it (altitude times distance, by trapezoids), worked
  out once with the profile.
- **`average_altitude(source, from, to)`**: the area between two
  distances over their distance, which is the average of the profile over
  it, weighted by distance rather than by record, so that the records'
  spacing doesn't matter. Clamped to the profile's ends.
- **Smoothing and columns in one**: a column's value is the average over
  its stretch widened by `smooth / 2` on each side. That's the centred
  average over `smooth` meters, averaged over the column, and needs no
  smoothed copy of the profile.

### Geometry: `overlay/ElevationProfile.h`

No raylib drawing, so that tests reach it all:

- **`profile_extent(source, profile, values)`**: from and to, in meters:
  the profile's ends for `"all"`, the current lap's first and last
  distances for `"lap"` (the whole activity without laps). None without a
  profile.
- **`profile_columns(source, extent, smooth, unit, columns)`**: a value a
  pixel column, in the unit shown.
- **`profile_bounds(columns, min, max, min_span)`**: `"auto"` is the
  columns' lowest and highest, which is what's drawn, so the line fills
  the box. `min_span` widens an `"auto"` scale up from its bottom; with
  only one of the two `"auto"`, that one moves.
- **`profile_points(columns, bounds, content)`** and
  **`split_at(points, x)`**: the line, and its `done` and `remaining`
  parts, which share the point at the marker.
- **`profile_marker(points, extent, distance, content)`**: the marker on
  the line where the frame's distance is, between two columns' points.
  None before the first distance, or outside the extent.
- **`lap_starts(source, extent)`**: the distances where laps start, the
  first lap's excepted, within the extent.
- **`label_box(marker, radius, label_size, content)`**: above and to the
  right of the marker, to its left past the right edge, below it where
  there's no room above, and inside the content box.

### Drawing

- The graph's line and fill under it become one renderer function,
  `draw_trace(points, trace, bottom)`, for both. The profile draws its
  panel, the `remaining` and then the `done` trace, the lap lines, the
  drop line, the marker with its outline as a ring, the axis labels at the
  content's top and bottom left, and the marker label.
- Every frame works the columns out again: an average is two binary
  searches, so 900 columns are far less than a frame's text. If the
  render's speed says otherwise, the columns of `"all"` can be kept.

### Built-in layout

- `cycling-dashboard` gets a profile at the top in the middle, 900×120,
  with the label and the altitude labels.

## Test plan

- `OverlayLayoutTest`: decoding profiles (extents, defaults, `done` and
  `remaining`, markers with `drop_line` as `true` and as a stroke, the
  labels' style cascade over a named style and the element's), the
  errors, fonts and codepoints of labels, and the deferred properties
  being taken.
- `OverlayFieldsTest`: `average_altitude()` over a known profile, between
  records, past the ends, and across a stop's drift.
- `OverlayElevationProfileTest` (new): extents for `"all"` and `"lap"`,
  and without laps or distances; columns smoothing a sawtooth, and with
  `smooth: 0`; bounds with `"auto"`, `min_span` and numbers; splitting;
  the marker between columns, before the first distance and held in a
  pause; lap starts; the label's four places and clamping.
- `LayoutFileTest`: `cycling-dashboard` still decodes, with its profile.
- By hand: the dashboard's profile in the preview and in a render, with
  `"all"` and `"lap"`, and the render's speed against before.

## Review notes

*Filled in by the Reviewer.*
