# Overlay gauges and graphs

Status: In progress

## Goal

The overlay can show a value as a shape, not only as a number: a bar or an
arc that fills with power or speed, and a graph of a field over the last
minute, or over the whole activity, with a cursor at the moment. Numbers
are exact, but a filling bar or a trace of heart rate is what a viewer
takes in at a glance, while watching the footage.

This is slice 4 of the [overlay roadmap](overlay-layout.md#roadmap),
after [overlay-fields.md](overlay-fields.md). The format is in the layout
reference, under [`gauge`](overlay-layout.md#gauge-bar-or-arc) and
[`graph`](overlay-layout.md#graph-any-field-over-time-or-distance), with
[strokes and fills](overlay-layout.md#strokes-and-fills). These are the
first elements that aren't text, so this slice also brings the drawing of
shapes into the overlay: the gauges and graphs, and the panels behind
elements, which layouts already accept but nothing draws yet.

## Feature

### `gauge`: bar or arc

```json
{ "type": "gauge", "field": "power", "shape": "arc",
  "min": 0, "max": 400,
  "track": "#ffffff30", "fill": "#ffb703", "thickness": 14,
  "at": { "x": -40, "y": -40, "anchor": "bottom-right" },
  "size": { "width": 200, "height": 200 } }
```

- `shape` is `bar` (horizontal, filling from the left), `vbar` (vertical,
  filling from the bottom) or `arc`: 270°, open at the bottom like a
  speedometer, filling clockwise from the bottom left. Other angles are
  *(deferred)*.
- `min` and `max` are a number, in the unit shown, or `"auto"`: the field's
  range over the activity, worked out from the records, for any number
  field. (The reference allowed `"auto"` only for the five fields that
  `Segment` has a range for.)
- `color_by` takes thresholds as a `value` does
  ([overlay-fields.md](overlay-fields.md#colors-by-value)), and colors the
  fill: a power arc can turn red above threshold.
- `track` is the fill of the empty part, `fill` of the part up to the
  value, and `thickness` how thick a bar or the arc is, in design pixels.
- A `bar` spans its box's width, and is `thickness` high, centred in the
  box; without a `thickness`, it's the box's height. A `vbar` is the same,
  upright. Its ends are square; rounded ends are *(deferred)*, and a panel
  with a `radius` around the bar gives most of that look.
- A value below `min` or above `max` fills to the end it's past. A missing
  value shows the track only.
- A gauge draws only the shape. A number with it is a `value` element of
  its own, such as in the arc's middle.

### `graph`: a field over time or distance

```json
{ "type": "graph", "field": "heart_rate", "x_axis": "time",
  "window": 60,
  "line": { "color": "#f94144", "width": 3 },
  "fill": "#f9414430",
  "cursor": { "color": "#ffffff", "radius": 6 },
  "at": { "x": 40, "y": -200, "anchor": "bottom-left" },
  "size": { "width": 400, "height": 100 } }
```

- `x_axis` is `time` or `distance`.
- `window` is how much of the activity the graph shows, ending at the
  moment: seconds on a `time` axis, meters on a `distance` axis. `"all"`
  shows the whole activity, with the cursor moving along it.
- `line` is a stroke, `fill` the area under it, and `cursor` a dot at the
  moment, on the line.
- `min` and `max` are as for a gauge, and `"auto"` by default: the field's
  range over the whole activity. So the scale never moves: a scale fitted
  to the window would jump as values come and go, and blow a flat
  stretch's noise up into mountains.
- **The moment is at the right edge** of a rolling window, also at the
  activity's start: the part before the start is empty, and the line grows
  in from the right. The graph's scale never changes, so nothing jumps.
- **Pauses** show as the fields do: power and speed drop to 0, and heart
  rate, missing, breaks the line, rather than a line being drawn across.
  On a `distance` axis, a pause takes no width: no distance goes by.
- **Missing values**, such as a power meter dropping out, break the line
  too. A graph of a field the activity hasn't got, or on a `distance` axis
  without distances, draws its panel only.
- **One point per pixel column**: the field's average over the column's
  stretch. With `"all"`, a 4-hour ride in 400 px is 36 s a column; one
  record a column would make a jagged line that flickers as the cursor
  moves. A band from each column's lowest to its highest value, which keeps
  power's spikes, is *(deferred)*.

### Panels

Every element can have a panel behind it, with the box style properties
the layout already accepts:

```json
"styles": {
    "panel": { "background": "#00000060", "radius": 16, "padding": 16,
               "border": { "color": "#ffffff40", "width": 1 } }
}
```

- `background` is a fill behind the element's box, with corners rounded
  by `radius`, in design pixels.
- `border` is a stroke around it.
- `padding` insets the element's content in its box: a graph's line, a
  text's text.
- Text and value elements have them too: a translucent panel behind a
  group of numbers keeps them readable over bright footage.

### Smooth edges

raylib draws shapes without anti-aliasing, and its multisampling works for
the window only, not for the render texture a video is drawn into. Arcs,
rounded panels and lines would have jagged edges, which show on video. So
a render draws the overlay at twice its size and scales it down, which
smooths every edge, text included. It draws four times the pixels but
reads the same number back; the Developer measures what it costs. Shaders
that draw each shape with soft edges would be better still, but need a
shader per shape. The preview, in the window, turns on multisampling.

### Version 1 scope

In: `gauge`, `graph` and panels, as above, and smooth edges. If the graph
turns out bigger than expected, it can be split off into a spec of its
own.

Also a new built-in layout, **`cycling-dashboard`**: values in a panel, a
power arc colored by thresholds, and a 60 s heart rate graph. It's an
example to copy, and what the hand test needs. `cycling-basic` stays plain
numbers.

*(Deferred)*, as in the reference: `max` relative to FTP or max heart
rate, which needs the athlete profile. Also other arc angles, rounded bar
ends, and min-max bands in graphs. And gauges and graphs of pace: it's a duration,
whose `min` and `max` would need the `"4:30"` syntax deferred with pace
zones, and a pace graph is usually upside down, faster higher up.

## Open questions

None.

## Architecture

The layout's new elements are decoded in `overlay/Layout`, their geometry
and the graph's sampling are plain functions in a new `overlay/Shapes`,
and `overlay/Renderer` draws them. Smoothing is in `overlay/OverlayRender`
for a video, and in the Widget library's `App` for the window.

### The layout

- **`Stroke { color, width }`** and **`Panel { background, radius,
  padding, border }`**, in design pixels. Every element gets a `panel`,
  decoded from the box style properties, which every element already
  accepts and ignores.
- **`GaugeElement`**: `field`, `unit` (as a value's), `shape` (`Bar`,
  `VBar`, `Arc`), `min` and `max` (`std::optional<double>`: none is
  `"auto"`), `track`, `fill`, `thickness` (optional) and `thresholds`, from
  `color_by` as for a value.
- **`GraphElement`**: `field`, `unit`, `axis` (`Time`, `Distance`),
  `window` (`std::optional<double>`: none is `"all"`), `min`, `max`, `line`
  (a stroke), `fill`, `cursor` (`{ color, radius }`).
- `OverlayElement` becomes a variant of the four, and `element_number()`
  and `layout_codepoints()` visit them all. `gauge` and `graph` leave
  `LaterTypes`, and their properties `LaterProperties`.
- **Defaults**, for what an element doesn't say: a gauge's `fill` is its
  `color`, its `track` the color at a fifth of its alpha; a graph's `line`
  is its `color`, 2 px wide; no graph `fill` and no `cursor` unless asked
  for, and a cursor without a color is the line's. A graph without a
  `window` shows `"all"`. Gauges and graphs take `color` but no other text
  style, and have no fonts: `element_fonts()` says which an element has.
- **Errors**: a gauge or a graph needs a `size`; its field has to be a
  number (as for `color_by`: not pace, see Version 1 scope); `min` has to be below `max`; `window` is
  more than 0, or `"all"`.

### Shapes and graphs: `overlay/Shapes.h`

No raylib drawing, so that tests reach it all:

- **`content_box(box, padding)`**: the box inset by its padding; and for a
  text or value without a `size`, the box is the content plus the padding.
- **`gauge_fraction(shown, min, max)`**: 0 to 1, clamped; none when
  missing.
- **`shown_bounds(source, field, unit, min, max)`**: the element's
  bounds, or the activity's range in the unit shown for `"auto"`; none if
  the activity hasn't got the field. Bounds that meet, such as a field
  that never changes, are 1 apart rather than dividing by 0.
- **`bar_rects(content, shape, thickness, fraction)`**: the track's and
  the fill's rectangles, centred across, filling from the left or the
  bottom.
- **`arc_ring(content, thickness)`**: the arc's centre and radii, as big
  as fits. An arc without a `thickness` is a fifth of its radius thick,
  as the spec gives no default for arcs.
- **`arc_angles(fraction)`**: in raylib's degrees (0 is to the right,
  clockwise on screen), from 135°, the bottom left, over 270°.
- **`graph_columns(source, element, now, columns)`**: one value a pixel
  column, in the unit shown, or none for a break:
  - the column's stretch is its share of the window ending at `now`
    (the frame's `elapsed`, or its `distance` on a distance axis), or of
    the whole activity for `"all"`; before the start it's empty;
  - the average of the records in the stretch, or the value at its
    middle when there are none, as in a pause or a stretch shorter than a
    record, which is what most rolling windows have;
  - the last column takes the record at its end too, which is the
    activity's last with `"all"`.
- **`graph_now(graph, values)`**: the frame's `elapsed` or `distance`, and
  **`graph_position(source, graph, now)`**: where the cursor goes, from 0
  to 1, at the right edge of a rolling window and moving along with
  `"all"`.
- **`graph_y(value, min, max, content)`**: where a value goes, clamped
  to the box.

### The activity's ranges

- **`OverlaySource::range(field)`**, for `"auto"`: the lowest and highest
  value over the records, in the base unit, worked out once with the
  rest of the source. The unit converts both ends, since every unit only
  multiplies and adds. A field has a range if a record has it, so the
  ranges replace the `recorded` flags, and `has()` reads them.
- `field_value()` gives a record's grade, over the default window, so
  that grade has a range and can be graphed like the other fields.
- **`OverlaySource::mileage`**: the records with a distance, in order of
  it, as the profile is. A graph on a distance axis searches it for a
  column's records, and for the time at a column's middle.

### Drawing

- The renderer has a `draw()` per element type. Each draws its panel
  first: `DrawRectangleRounded()` and `DrawRectangleRoundedLinesEx()`,
  with raylib's roundness worked out from the radius, then its content in
  the content box.
- Gauges: `DrawRectangleRec()` for bars, `DrawRing()` for the arc. Graphs:
  the area under each unbroken run as triangles, the run as
  `DrawSplineLinear()`, which has a width, and the cursor as a circle.

### Smooth edges

- **Video**: `OverlayRender` draws each frame into a render texture twice
  the output's size, and that, with bilinear filtering, into one of the
  output's size, which averages each 2×2 block; that's what's read back.
  The colors are premultiplied, which is what filtering needs; the copy
  uses raylib's premultiplied blend, which onto the cleared frame copies
  them. Measured at 1080p: 300 frames of the dashboard took 3.5 s with
  the doubled texture and without, since ffmpeg's encoding is what limits
  a render. The fonts load at the doubled size
  by themselves, since the renderer scales to its area. At 4K the large
  texture is 7680×4320, within the 16384 textures can be.
- **Window**: `App::create()` asks for 4× multisampling before
  `InitWindow()`. It's for the whole window: the rest of the app's shapes
  get smoother edges too.

### Built-in layout and README

- `share/overlays/cycling-dashboard.overlay.json`, and the README's
  overlay section shows a gauge, a graph and a panel. Until groups come,
  a panel behind several elements is a `text` without text, with a size.

## Test plan

- `OverlayLayoutTest`: decoding gauges and graphs (shapes, `"auto"` and
  numbers, defaults, `color_by`, `window`, strokes, cursors), panels on
  every element type, and the errors; `LaterTypes` no longer skipping
  them.
- `OverlayShapesTest` (new): `content_box()`, `gauge_fraction()` (in the
  unit shown, clamped, missing), `bar_rects()` for both shapes and a
  thickness, `arc_angles()`, and `graph_columns()`: a rolling window at
  the start, a pause (0 for power, a break for heart rate), a dropout,
  `"all"` over a long activity averaging, a distance axis through a pause,
  no distances; `graph_y()` clamping.
- `OverlayFieldsTest`: `OverlaySource::range()`.
- `LayoutFileTest`: `cycling-dashboard` decodes with shipped fonts.
- By hand: the dashboard in the preview and in a render, edges in Final
  Cut at 1080p, a pause in a heart rate graph, and what the render's
  speed is with the doubled texture, against before.

## Review notes

*Filled in by the Reviewer.*
