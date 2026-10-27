# Overlay gauges and graphs

Status: Ready

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
ends, and min-max bands in graphs.

## Open questions

None.

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
