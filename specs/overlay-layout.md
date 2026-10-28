# Overlay Layout Format

This defines the file that describes what the transparent overlay video from
[video-overlay.md](video-overlay.md) contains: which data is shown
where, in which font, size and color, plus static labels, graphs, and the GPS
track with an optional OpenStreetMap underlay.

This is the design reference for the overlay, not a feature spec: it has
no Status. The overlay is built in slices, each with a spec of its own that
says which parts of this reference it builds (see [Roadmap](#roadmap)).
Decisions made in a slice's spec that change the design are brought back
here, so that this stays the one description of the format. Features marked
*(deferred)* aren't in any slice yet.

## Format: JSON

We use JSON rather than XML:

- `src/Lib/Base/JSON.h` already parses JSON, and `share/settings.json` is
  JSON too.
- Layouts are small and hand-edited, so they may have `//` and `/* */`
  comments. The JSON lexer looks like it skips them already, unless it's
  asked to keep them; the first slice confirms that with a test. No JSON5.

`decode<T>()` doesn't decode structs generically: it calls a hand-written
`T::decode(JSONValue const &)` (see `Strava::Config::decode` for an example).
The layout format has many properties that take more than one shape (`x` is a
number or `"50%"`, `style` is a string or an array, `resolution` is a preset
or an object, ...), and the style cascade has to merge JSON objects before
anything is decoded. So loading a layout is:

1. Parse the file.
2. For each element, merge `defaults`, its named styles, and its own
   properties into one JSON object (later wins, objects merge recursively).
3. Decode the merged object with a hand-written decoder per element type.

Expect several hundred lines for the decoders. Colors are `#rrggbb` or
`#rrggbbaa`. There's no color parser in the code yet (theme colors are read but
never parsed), so one has to be written.

## Two files: layout and render job

What the overlay looks like is reusable, but which activity it's rendered for,
and at which size, is not. So there are two files:

- **Layout** (`*.overlay.json`): a template. "Speed in the bottom left, heart
  rate next to it, map in the top right." It has no references to a specific
  activity, and no output size. Built-in layouts live in `share/overlays/`.
- **Render job**: the activity, the layout, and the output settings. The app
  can build this in the UI and doesn't necessarily need to save it, but a file
  makes command-line and batch rendering possible:

```json
{
  "layout": "cycling-basic",
  "activity": "2025-06-14-0832",
  "resolution": "1080p",
  "fps": "30000/1001",
  "range": { "from": "1:12:00", "to": "1:20:30" },
  "output": "~/Movies/ride.overlay.mov",
  "codec": "prores4444"
}
```

- **`resolution`**: a preset (`720p`, `1080p`, `1440p`, `4k`, each with a
  `-portrait` variant such as `1080p-portrait`) or an explicit
  `{ "width": 1920, "height": 1080 }`.
- **`fps`**: the frame rate, as an integer or an exact rational such as
  `"30000/1001"`. It should match the Final Cut project.
- **`range`**: the part of the activity to render. Leave it out to render the
  whole activity. `from` and `to` are either:
  - elapsed **wall-clock** time since the activity start, as `h:mm:ss`
    (`"1:12:00"`). Pauses count, because the footage runs on the wall clock
    too.
  - a local time of day with an `@` prefix (`"@09:44:00"`). That's what the
    user reads off the camera's file info.
- **`codec`**: `prores4444` (default) or `hevc-alpha` *(deferred)*.

Frame `n` shows the activity at `range.from + n / fps`. Compute this with
integer math on the rational frame rate (`n * 1001 / 30000` seconds), not by
adding up floating-point frame durations, which drifts over long renders.

There's no video in the render job. Lining the overlay up with the footage is
done by hand in Final Cut (see [video-overlay.md](video-overlay.md)).

## Layout structure

```json
{
  "version": 1,
  "design_size": { "width": 1920, "height": 1080 },
  "units": "metric",
  "defaults": {
    "font": "Inter-Bold.ttf",
    "font_size": 48,
    "color": "#ffffff",
    "shadow": { "color": "#00000080", "offset": [2, 2] }
  },
  "styles": {
    "label": { "font_size": 22, "color": "#ffffffb0", "font": "Inter-Regular.ttf" },
    "value": { "font_size": 64 },
    "panel": { "background": "#00000060", "radius": 16, "padding": 16 }
  },
  "elements": [ ... ]
}
```

- **`design_size`**: the size the coordinates were designed for. If the
  render job's resolution differs (for example 4K or portrait), every position
  and size is scaled by `min(out_w / design_w, out_h / design_h)`, so one
  layout works for 1080p and 4K. Portrait renders (9:16) don't get anything
  of their own: anchors keep elements at their edges, and a layout that's
  too cramped in portrait, such as a map with a profile along the bottom,
  is better made as a second, portrait layout.
- **`units`**: `metric` (the default) or `imperial`. Sets the default unit of
  each field (see below). Individual elements can override it. It's in the
  layout rather than an app setting, so that a layout looks the same for
  everyone who uses it; an app setting that layouts without `units` inherit
  can come later *(deferred)*.
- **`defaults`** and **`styles`**: style properties that cascade. An element
  takes `defaults`, then each style named in its `"style"` (a string or an
  array, applied in order), then its own properties.
- **Fonts**: the examples use Inter, which isn't shipped yet (`share/fonts`
  only has VictorMono). Built-in layouts may only use fonts that are shipped
  in `share/fonts`, so Inter (SIL Open Font License) needs to be added, or the
  examples changed.

### Strokes and fills

The same two shapes are used everywhere a line or an area is drawn:

- A **stroke** is `{ "color": "#rrggbbaa", "width": 3 }`.
- A **fill** is a color string.

So `graph.line`, `profile.done.line`, `map.track.done` and `border` are all
strokes; `graph.fill` and `profile.done.fill` are fills.

Every element can have a panel behind its box: `background` (a fill),
`radius` for its corners, `border` (a stroke), and `padding`, which insets
the content. See [overlay-gauge-graph.md](overlay-gauge-graph.md#panels).

### Positioning

Every element has a box:

```json
"at": { "x": 40, "y": -40, "anchor": "bottom-left" },
"size": { "width": 400, "height": 120 }
```

- `x` and `y` are design pixels, or percentages such as `"50%"` of the
  canvas's width or height. The sign picks the edge: positive values count
  from the left or top edge, negative ones from the right or bottom edge.
- `anchor` is the point of the element that is placed at (x, y):
  `top-left` (the default), `top`, `top-right`, `left`, `center`, `right`,
  `bottom-left`, `bottom`, `bottom-right`. So `bottom-right` at `-40, -40`
  puts the element's bottom right corner 40 pixels in from the canvas's,
  which keeps it in place when the aspect ratio changes, and `bottom` at
  `"50%", -40` centers it, 40 pixels up from the bottom. (An earlier
  version also measured the coordinates from the anchor's corner of the
  canvas, which put `"50%"` with `bottom` at the right edge.)
- `size` is the element's box. The font's size is `font_size`, so that
  the two don't overwrite each other in the cascade.
- Elements are drawn in array order, so later elements are drawn on top.

### Visibility over time

Any element can have:

```json
"visible": { "from": 5, "to": "end" },
"fade": 0.5
```

`from` and `to` are seconds from the start of the rendered range, so a title
card can show for the first few seconds. `fade` is the fade-in and fade-out
time in seconds.

*(Deferred)* `"when": "lap_change"` shows an element for a few seconds after
each new lap.

## Element types

### `text`: static labels and templates

```json
{ "type": "text", "text": "Col du Galibier", "style": "label",
  "at": { "x": 40, "y": 40 } }

{ "type": "text", "text": "Lap {lap} · {distance:.1f} {distance.unit}",
  "at": { "x": 40, "y": 100 } }
```

`{field}` placeholders are filled in on every frame. `align` can be `left`,
`center` or `right`: where the text goes across a box given with `size`.
Without it, the text sits in its box where its anchor is, as a `value`
does, so that a box anchored `bottom-left` has its content at its bottom
left. See [Templates and formatting](#templates-and-formatting)
for the placeholder syntax.

### `value`: one data field, formatted

This is shorthand for the most common case: a large number with a small unit
next to it.

```json
{ "type": "value", "field": "speed", "format": "{:.1f}",
  "unit": "auto", "style": "value",
  "unit_style": { "font_size": 28 },
  "missing": "--",
  "at": { "x": 40, "y": -40, "anchor": "bottom-left" } }
```

- `missing` is what to show when the field has no value (for example no power
  meter). `"hide"` hides the element instead.
- `color_by` changes the color based on the value, for example heart rate or
  power zones:

  ```json
  "color_by": { "zones": "hr", "colors": ["#8ecae6", "#90be6d", "#f9c74f", "#f8961e", "#f94144"] }
  ```

  Zones need an athlete profile, which doesn't exist yet (see
  [Code prerequisites](#code-prerequisites)). Until then `color_by` takes
  explicit thresholds: `{ "thresholds": [[0, "#8ecae6"], [130, "#90be6d"], ...] }`,
  each the color from that value up, in the unit shown. Only the number
  changes color, not the unit; below the first threshold, and when
  missing, the element has its own color. Only `value` elements have
  `color_by`.

### `gauge`: bar or arc

```json
{ "type": "gauge", "field": "power", "shape": "arc",
  "min": 0, "max": { "of": "ftp", "factor": 1.5 },
  "track": "#ffffff30", "fill": "#ffb703", "thickness": 14,
  "at": { "x": -40, "y": -40, "anchor": "bottom-right" },
  "size": { "width": 200, "height": 200 } }
```

- `shape` is `bar` (horizontal), `vbar` or `arc` (270°, open at the
  bottom, filling clockwise).
- `min` and `max` are one of:
  - a number, in the unit shown.
  - `"auto"`: the field's range over the activity, from the records, for
    any number field.
- `color_by` colors the fill, as a `value`'s colors its number. See
  [overlay-gauge-graph.md](overlay-gauge-graph.md) for bars' thickness.
  - *(deferred)* `{ "of": "ftp" | "max_hr", "factor": 1.5 }`, which needs the
    athlete profile.

### `graph`: any field over time or distance

```json
{ "type": "graph", "field": "heart_rate", "x_axis": "time",
  "window": 60,
  "line": { "color": "#f94144", "width": 3 },
  "fill": "#f9414430",
  "cursor": { "color": "#ffffff", "radius": 6 },
  "at": { "x": 40, "y": -200, "anchor": "bottom-left" },
  "size": { "width": 400, "height": 100 } }
```

- `x_axis` is `distance` or `time`.
- `window: 60` shows a rolling 60-second trace ending at the current moment,
  which suits heart rate or power; on a `distance` axis it's meters.
  `window: "all"` shows the whole activity with a cursor at the current
  position.
- `min` and `max` are as for a gauge, `"auto"` by default, so the scale
  doesn't move. Missing values break the line; each pixel column is the
  average of its stretch. See [overlay-gauge-graph.md](overlay-gauge-graph.md).
- For elevation, use the more specific `profile` element below.

### `profile`: elevation profile with the current position

Shows the elevation of the whole ride (or a section of it) against distance,
with a marker at the current position. The part already ridden and the part
still to come can be styled differently, the same way the map track is.

```json
{ "type": "profile",
  "at": { "x": "50%", "y": -40, "anchor": "bottom" },
  "size": { "width": 900, "height": 140 },
  "extent": "all",
  "done":      { "line": { "color": "#ffb703", "width": 3 }, "fill": "#ffb70360" },
  "remaining": { "line": { "color": "#ffffff", "width": 3 }, "fill": "#ffffff30" },
  "marker": { "shape": "dot", "color": "#ffffff", "radius": 8,
              "outline": "#ffb703", "drop_line": true },
  "marker_label": { "text": "{altitude:.0f} {altitude.unit} · {grade:+.1f}%",
                    "style": "label" },
  "min": "auto", "max": "auto", "min_span": 100,
  "axis_labels": { "min_max": true, "distance": false, "style": "label" },
  "laps": { "color": "#ffffff60", "width": 1 } }
```

- **`extent`**: which part of the ride the x axis covers. (Called `extent`
  rather than `range` so it isn't confused with the render job's time range.)
  - `"all"` (default): the whole activity. The marker moves from left to
    right over the course of the render.
  - `{ "from": 12.5, "to": 31.0 }`: a fixed section in km (mi for imperial),
    for a render that only covers one climb.
  - `"lap"`: the current lap. The profile changes at each new lap, which
    works well for rides split into climbs.
  - *(deferred)* `{ "behind": 2, "ahead": 5 }`: a window in km around the
    current position, which scrolls past a fixed marker. It shows what's
    coming up on long rides, where a whole-ride profile is too compressed to
    read.
- **`done` and `remaining`**: line and fill before and after the current
  position. Leave out `remaining` to use `done` for the whole profile.
- *(deferred)* **`color_by_grade`**: fills the area under the profile by
  gradient instead of with `done.fill`/`remaining.fill`, as in climb profiles.
  The bands are steepness thresholds in %, each with a color:

  ```json
  "color_by_grade": { "bands": [[0, "#90be6d"], [4, "#f9c74f"], [7, "#f8961e"], [10, "#f94144"]],
                      "segment": 0.5, "remaining_alpha": 0.4 }
  ```

  The grade is averaged over `segment` km. `done.line` and `remaining.line`
  still draw the outline. The fill after the current position is drawn with
  its alpha multiplied by `remaining_alpha`.
- **`marker`**: the current position. `drop_line` draws a thin vertical line
  from the marker down to the baseline.
- **`marker_label`**: a `text` template drawn next to the marker that moves
  with it. It flips to the left side of the marker near the right edge, so it
  doesn't get cut off.
- **`min`, `max` and `min_span`**: the elevation range on the y axis, as
  [overlay-profile.md](overlay-profile.md) has them, rather than in a
  `scale` object. `"auto"` uses the minimum
  and maximum of the elevation inside `extent`. `min_span` (in m, ft for
  imperial) keeps a flat ride from being stretched until 5 m of rolling road
  looks like the Alps.
- **`axis_labels`**: `min_max` writes the lowest and highest elevation at the
  left edge. `distance` adds distance ticks along the bottom.
- **`laps`**: a stroke for thin vertical lines at lap boundaries. Leave it out
  for none.

Data and rendering:

- The elevation comes from `record.altitude`, with `record.distance` on the
  x axis. Records without a distance (for example indoor rides) can't be
  placed on a profile, and the element is hidden when the activity has none.
- Altitude is smoothed with a centered average over `smooth` meters (default
  20, see [overlay-profile.md](overlay-profile.md); this said 10 seconds) before drawing, because barometric and especially GPS altitude are
  jagged. Centered rather than trailing, because the whole shape is known in
  advance and a trailing average would shift the hills.
- The marker position is interpolated between records, like every other
  field (see [Sampling and smoothing](#sampling-and-smoothing)), so it glides
  instead of jumping once a second.
- For fixed extents the profile shape doesn't change during the render. Build
  the polyline once and on each frame only split it at the current position
  and draw the marker.

### `map`: GPS track, position, optional OSM underlay

```json
{ "type": "map",
  "at": { "x": -40, "y": 40, "anchor": "top-right" },
  "size": { "width": 420, "height": 420 },
  "clip": "circle",
  "border": { "color": "#ffffff", "width": 4 },
  "view": "follow",
  "zoom": 15,
  "tiles": {
    "source": "osm",
    "opacity": 0.8,
    "filter": "grayscale"
  },
  "attribution": { "style": "label", "font_size": 14 },
  "track": {
    "done":      { "color": "#ffb703", "width": 6 },
    "remaining": { "color": "#ffffff80", "width": 4 },
    "outline":   { "color": "#00000080", "width": 2 }
  },
  "marker": { "shape": "dot", "color": "#ffffff", "radius": 10,
              "outline": "#ffb703" } }
```

- **`view`**:
  - `fit`: the whole track fits in the box and doesn't move, and only the
    marker moves. This is what `Map::init(storage, box, w, h)` already does.
  - `follow`: the map is centered on the current position at a fixed `zoom`.
- *(deferred)* **`rotate`**: `"north"` (default) or `"heading"`, which turns the
  map so the direction of travel points up.
- **`clip`**: `rect`, `rounded` (with a `radius` property) or `circle`.
- **`tiles`**: leave the key out or use `null` to show only the track with no
  underlay. `source` is `osm` (`https://tile.openstreetmap.org/{z}/{x}/{y}.png`).
  *(Deferred)* custom URL templates. `filter` is `none`, `grayscale`, `dark`
  (inverted grayscale) or a tint color, to keep the map from competing with
  the footage.
- **`attribution`**: OSM's license requires "© OpenStreetMap contributors"
  wherever its tiles are shown, including in videos. It's always drawn when
  OSM tiles are shown and can't be turned off; the property only styles it.
  It's placed just below the map box, right-aligned, so a circular clip can't
  cut it off. *(Not verified: whether OSM's attribution guidelines would also
  allow a credit at the end of the video instead. Drawing it next to the map is
  the safe choice.)*
- **`zoom` at other resolutions**: when design scaling makes the map box
  bigger (factor 2 at 4K), drawing zoom-15 tiles at twice their size looks
  blurry. Instead, fetch tiles at `zoom + round(log2(scale))`, so the map shows
  the same area at full sharpness.

### `image`: logos and icons

```json
{ "type": "image", "src": "icons/heart.png", "tint": "#f94144",
  "at": { "x": 40, "y": -110, "anchor": "bottom-left" },
  "size": { "width": 32, "height": 32 } }
```

Paths are relative to the layout file, then to `share/overlays/`.

### `group`

```json
{ "type": "group", "style": "panel",
  "at": { "x": 40, "y": -40, "anchor": "bottom-left" },
  "size": { "width": 260, "height": 140 },
  "children": [
    { "type": "text",  "text": "HEART RATE", "style": "label", "at": { "x": 0, "y": 0 } },
    { "type": "value", "field": "heart_rate", "style": "value", "at": { "x": 0, "y": 30 } }
  ] }
```

- The group draws its own `background` (with `radius`) once, then its
  children.
- Children are positioned inside the group's box minus `padding`. A child's
  `anchor` and negative coordinates refer to that inner box, not the canvas.
- Children inherit only text properties from the group: `font`, `font_size`,
  `color` and `shadow`. Box properties (`background`, `radius`, `padding`,
  `border`) are not inherited, so the panel isn't drawn again behind every
  child.

## Fields

These map onto `FIT::record`, `Lap` and `Segment`. Every field `f` also has
`f.unit` (the unit string for templates).

| Field         | Source                                    | Metric unit | Imperial unit |
|---------------|-------------------------------------------|-------------|---------------|
| `speed`       | `record.speed`                            | km/h        | mph           |
| `pace`        | derived from `speed`                      | min/km      | min/mi        |
| `heart_rate`  | `record.heart_rate`                       | bpm         | bpm           |
| `power`       | `record.power`                            | W           | W             |
| `cadence`     | `record.cadence` (see below)              | rpm / spm   | rpm / spm     |
| `altitude`    | `record.altitude`                         | m           | ft            |
| `grade`       | derived from altitude and distance        | %           | %             |
| `distance`    | `record.distance`                         | km          | mi            |
| `temperature` | `record.temperature`                      | °C          | °F            |
| `lr_balance`  | `record.left_right_balance` (see below)   | %           | %             |
| `elapsed`     | wall-clock time since activity start      | h:mm:ss     | h:mm:ss       |
| `time_of_day` | `record.timestamp` + `Activity.utc_offset`| HH:MM       | HH:MM         |
| `lap`         | current lap, 1-based                      |             |               |
| `title`       | `Activity.title`                          |             |               |
| `date`        | `Segment.start_time`                      |             |               |

*(Deferred)*: `power_wkg` (needs athlete weight), and `f.avg`, `f.max` and
`f.lap_avg` for numeric fields (average and maximum from the start up to now,
and average over the current lap; these need prefix sums over the records).

`unit` on an element is `"auto"` (from `units`) or an explicit unit such as
`"m/s"`.

Notes per field:

- **`pace`**: missing when the speed is below 1 km/h. Otherwise slow walking
  or standing still shows absurd values like 600:00 min/km.
- **`cadence`**: for cycling, rpm as recorded. For running, FIT records strides
  per minute of one foot, so steps per minute is `2 × cadence`.
  (`fractional_cadence` isn't parsed and is ignored.)
- **`grade`**: computed as the change in altitude over the change in
  distance across a 30 m window centred on the moment; a `value` element
  can set `"window"`, 10 to 200 m. (100 m smoothed short, steep ramps
  away; see [overlay-fields.md](overlay-fields.md).) `record.grade` is not
  used: most devices don't record it.
- **`lr_balance`**: the raw value is a bitfield. Bit `0x80` set means the
  value is the right leg's share, bits `0x7F` are the percentage. Display it
  as left/right, e.g. `48/52`. The raw value `0xFF` means invalid, and
  without bit `0x80` the side is unknown, so it's missing.
- **`time_of_day`**: FIT timestamps are UTC. `Activity.utc_offset` (local time
  minus UTC, from the FIT activity message) converts them to the local time
  where the activity happened. If the file has no local timestamp, fall back
  to the system time zone.

### Templates and formatting

- Placeholders are `{field}`, `{field:spec}` and `{field.unit}`. A small
  template parser splits the text and formats each placeholder separately;
  the text can't go to `std::vformat` as is, because `.unit` isn't
  `std::format` syntax.
- Numeric fields are always passed to the formatter as `double`, whatever their
  type in `record`. (`std::format("{:.1f}", u8)` throws.) So `{heart_rate}`
  prints `142` and `{heart_rate:.0f}` does the same thing; the default spec
  for each field is the precision a human expects (integer for heart rate and
  power, one decimal for speed and distance).
- Durations (`elapsed`) take `hms` (`1:05:09`, default) or `ms` (`65:09`).
- `time_of_day` takes `hm` (`09:44`, default) or `hms` (`09:44:10`).
- `date` takes `std::chrono` style specs, `%d %b %Y` by default
  (`14 Jun 2025`), in English.
- `pace` takes `ms` (`4:35`, default).

### Sampling and smoothing

Records usually arrive at 1 Hz and the video runs at 30 or 60 fps, so a value
that changes once a second looks jumpy.

- When an activity is loaded, `Segment::analyze` already overwrites speed,
  power, cadence and heart rate in the records with a 3-sample moving average.
  So the overlay's own smoothing defaults to none for those fields. Elements
  can set `"smooth": <seconds>` for more, as a trailing average.
  *Note for the slice that builds this:* a profile's `smooth` is in meters
  ([overlay-profile.md](overlay-profile.md)), and both cascade from the
  defaults and styles, so a `"smooth": 3` in the defaults meant for values
  would reach every profile as 3 m. Settle it then: such as a profile only
  taking a `smooth` of its own, or another name for one of the two.
- Numeric fields and the map marker are **interpolated** linearly between the
  two records around the current frame time. Displayed integers (heart rate,
  power) are rounded after the interpolation. `"interpolate": false` turns
  this off.

### Pauses and missing data

If the two records around the current frame are more than `max_gap` seconds
apart (default 5), the activity was paused (auto-pause, or the user stopped
the timer) or lost its signal. During a gap:

| Fields                                             | Shown as            |
|----------------------------------------------------|---------------------|
| `speed`, `power`, `cadence`                        | 0                   |
| `pace`                                             | missing             |
| `heart_rate`, `temperature`, `lr_balance`          | missing             |
| `distance`, `altitude`, `grade`, map position      | held at last value  |
| `elapsed`, `time_of_day`                           | keep running        |
| `lap`                                              | held                |

A field that the activity doesn't have at all (no power meter, an indoor ride
without GPS) is missing for the whole render. A `map` with no positions and a
`profile` with no distances are hidden.

## Rendering notes

- **Fonts** are resolved through `appearance.font_directories` from
  `settings.json`. Each distinct (font, scaled size) pair is loaded once with
  `LoadFontEx` at the exact pixel size, because scaling a small font up blurs
  it. Load only ASCII plus the characters the layout actually uses (`·`, `°`,
  `±` and so on). `App::set_fonts` loads 992 code points, which makes a huge
  glyph atlas at 128 px (64 px scaled for 4K).
- **Shadow/outline**: draw the text again with a hard offset in the shadow
  color. On top of arbitrary footage, text without a shadow or outline is hard
  to read, which is why `defaults` includes a shadow. *(Deferred)* blurred
  shadows, which need a two-pass blur shader and an extra render texture.
- **Alpha blending**: see [video-overlay.md](video-overlay.md#alpha). Every
  element is drawn with the overlay's custom blend mode.
- **Circle and rounded clipping**: raylib has no stencil API. Draw the map
  into its own `RenderTexture2D` the size of the map box, then draw that
  texture onto the overlay with a small fragment shader that discards pixels
  outside the circle or rounded rectangle. The same approach later handles
  rotation. `rect` can use `BeginScissorMode`.
- **Map tiles**: don't stitch all tiles in the track's bounding box into one
  texture. A 100 km point-to-point ride at zoom 15 has a bounding box of
  roughly 100 × 100 tiles: far beyond the maximum texture size (often 16384
  px), and downloading them all would be bulk downloading under OSM's policy.
  Instead:
  - Fetch only the tiles along the track: every tile the track passes through,
    plus enough neighbors to fill the map box around any point on the track.
  - Keep each tile as its own texture, and each frame draw the few tiles in
    view (3 × 3 to 5 × 5).
  - In `fit` mode, there's one fixed set of tiles, as `Map::init` already
    computes.
- **OSM tile usage policy**
  ([policy](https://operations.osmfoundation.org/policies/tiles/)):
  - Send a User-Agent that identifies the app and a way to contact us (e.g.
    `SweatTrails/1.0 (+https://github.com/...)`). The current
    `sweattrails/1.0` lacks the contact part.
  - Respect the HTTP caching headers. The tile cache currently never expires,
    and the policy asks for at least 7 days of caching. That's satisfied, but
    tiles also never get refreshed.
  - Throttle downloads to a couple of parallel requests.
- **Preview**: the same renderer should draw into the activity screen with a
  time scrubber, so layouts can be adjusted without encoding a video. Hot
  reload when the layout file changes would make editing layouts by hand
  bearable. That is the way to edit layouts: there's no graphical layout
  editor.

## Code prerequisites

Fixed while writing this spec:

- `Activity::analyze()`, which splits the records into laps, was never called,
  so laps had no records. It's now called after loading. Its lap splitting was
  also broken for more than two laps and has been rewritten.
- Lap and session end times were computed as start + moving time, which is too
  early when there were pauses. They now use the message's timestamp, which
  FIT defines as the end time.
- `record.grade` was read without its scale of 100.
- `share/settings.json` used `${ARAGORN_DATADIR}`, but the code replaces
  `${sweattrails_DATADIR}`, so `share/fonts` was never found. Subdirectories
  of font directories were also added as the parent directory again.
- `Activity.utc_offset` now stores the local time offset from the FIT
  activity message.

Still to do:

- A color parser for `#rrggbb[aa]`.
- The layout decoders (see [Format: JSON](#format-json)).
- Ship the fonts the built-in layouts use.
- Tile code: pass the URL template in instead of hard-coding the OSM URL in
  `Tile::get_tile`, and key the cache by source. Otherwise a custom tile
  server's tiles mix with OSM's in `tiles_dir/z/x/y.png`. Also fix the
  User-Agent, and turn off `CURLOPT_VERBOSE` in `download()`.
- *(Deferred features only)* an athlete profile with FTP, max HR, weight and
  zones, replacing the `JANS_FTP` constant.

## Roadmap

Each slice is a spec of its own, written when its turn comes, and small
enough for one Developer and one Reviewer. Rendering to video comes second,
so that a video with only speed and power over the footage is useful early,
and the parts that could go wrong (alpha, ffmpeg, timecode) are tried before
most of the elements exist.

1. **[overlay-preview.md](overlay-preview.md)**: the layout file (`defaults`,
   `styles` and the cascade, `design_size`, positioning, colors), the `text`
   and `value` elements with hard shadows and a few fields, drawn in the app
   as a preview at the mark, and reloaded when the file changes. One
   built-in layout, and the fonts it uses.
2. **Rendering to video** (now [video-overlay.md](video-overlay.md), to be
   brought into the template): the render job, ffmpeg and ProRes 4444, the
   alpha blend mode, the timecode, the range, and interpolation between
   records, which matters between frames.
3. **Fields**: the other fields in the table, the pause rules, the template
   specs (`hms`, `ms`, `hm`, `date`), and `color_by` thresholds.
4. **`gauge` and `graph`**.
5. **`profile`**.
6. **`map`**: `fit` and `follow`, clipping through a render texture and a
   shader, tile filters, the tiles along the track, and the tile code
   prerequisites. The biggest slice, which may be split again.
7. **`group`, `image`, and `visible`/`fade`**.

Deferred, in no slice yet:

- HEVC with alpha.
- `rotate: "heading"` for maps.
- Custom tile sources.
- `color_by_grade` and scrolling `extent` windows for profiles.
- `when: lap_change`.
- Athlete profile: zones, gauge limits relative to FTP or max HR,
  `power_wkg`.
- `f.avg`, `f.max`, `f.lap_avg`.
- Blurred shadows.
- An app setting for `units`.
