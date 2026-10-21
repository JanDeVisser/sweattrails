# Overlay Layout Format

This defines the file that describes what the transparent overlay video from
[video-overlay.md](video-overlay.md) contains: which data is shown
where, in which font, size and color, plus static labels, graphs, and the GPS
track with an optional OpenStreetMap underlay.

## Format: JSON

We use JSON rather than XML:

- `src/Lib/Base/JSON.h` already parses JSON and has the `decode<T>()`
  machinery, so the layout can be decoded straight into structs.
- `share/settings.json` and the themes are already JSON, and colors use the
  same `#rrggbb[aa]` notation as the themes.
- Layouts are small and hand-edited. Comments would be nice, though, so the
  parser could accept `//` comments in layout files. (Open question.)

## Two files: layout and render job

What the overlay looks like is reusable, but which video and activity go
together is not. So there are two files:

- **Layout** (`*.overlay.json`): a template. "Speed in the bottom left, heart
  rate next to it, map in the top right." It has no references to a specific
  video or activity. Built-in layouts live in `share/overlays/`.
- **Render job**: the video, the activity, the time offset between them, and
  which layout to use. The app can build this in the UI and doesn't
  necessarily need to save it, but a file makes command-line and batch
  rendering possible:

```json
{
  "layout": "cycling-basic",
  "video": "~/Movies/ride.mov",
  "activity": "2025-06-14-0832",
  "offset": 754.2,
  "range": { "from": 0, "to": "end" },
  "output": "~/Movies/ride.overlay.mov",
  "codec": "prores4444"
}
```

`offset` is the activity time in seconds at video frame 0. It starts as the
value computed from `com.apple.quicktime.creationdate` and the user adjusts it
by hand.

## Layout structure

```json
{
  "version": 1,
  "canvas": { "width": "source", "height": "source", "fps": "source" },
  "design_size": { "width": 1920, "height": 1080 },
  "units": "metric",
  "defaults": {
    "font": "Inter-Bold.ttf",
    "size": 48,
    "color": "#ffffff",
    "shadow": { "color": "#00000080", "offset": [2, 2], "blur": 4 }
  },
  "styles": {
    "label": { "size": 22, "color": "#ffffffb0", "font": "Inter-Regular.ttf" },
    "value": { "size": 64 },
    "panel": { "background": "#00000060", "radius": 16, "padding": 16 }
  },
  "elements": [ ... ]
}
```

- **`canvas`**: the output size and frame rate. `"source"` means "match the
  video", which should nearly always be the right value.
- **`design_size`**: the size the coordinates were designed for. If the output
  size differs (for example a 4K or portrait source), every position and size
  is scaled by `min(out_w / design_w, out_h / design_h)`, so one layout works
  for 1080p and 4K. The scaling might be done per axis instead. (Open
  question.)
- **`units`**: `metric` or `imperial`. Sets the default unit of each field (see
  below). Individual elements can override it.
- **`defaults`** and **`styles`**: style properties that cascade. An element
  takes `defaults`, then each style named in its `"style"` (a string or an
  array, applied in order), then its own properties.

### Positioning

Every element has a box:

```json
"at": { "x": 40, "y": -40, "anchor": "bottom-left" },
"size": { "width": 400, "height": 120 }
```

- `x` and `y` are design pixels, or percentages such as `"50%"`. Negative
  values count from the right or bottom edge.
- `anchor` is the point of the element that is placed at (x, y), and also the
  canvas corner the coordinates are measured from: `top-left`, `top`,
  `top-right`, `left`, `center`, `right`, `bottom-left`, `bottom`,
  `bottom-right`. This keeps a bottom-right element in place when the aspect
  ratio changes.
- Elements are drawn in array order, so later elements are drawn on top.
- A `group` element positions its `children` relative to its own box. This
  covers the common case of a panel holding a label and a value.

### Visibility over time

Any element can have:

```json
"visible": { "from": 5, "to": "end" },
"fade": 0.5
```

`from` and `to` are video seconds, so a title card can show for the first few
seconds. An element can also have `"when": "lap_change"` to show for a few
seconds after each new lap. (Possible later addition.)

## Element types

### `text`: static labels and templates

```json
{ "type": "text", "text": "Col du Galibier", "style": "label",
  "at": { "x": 40, "y": 40 } }

{ "type": "text", "text": "Lap {lap} · {distance:.1f} {distance.unit}",
  "at": { "x": 40, "y": 100 } }
```

`{field}` placeholders are filled in on every frame. The format after `:`
follows `std::format` syntax. `align` can be `left`, `center` or `right`.

### `value`: one data field, formatted

This is shorthand for the most common case: a large number with a small unit
next to it.

```json
{ "type": "value", "field": "speed", "format": "{:.1f}",
  "unit": "auto", "style": "value",
  "unit_style": { "size": 28 },
  "missing": "--",
  "at": { "x": 40, "y": -40, "anchor": "bottom-left" } }
```

- `missing` is what to show when the field isn't in the record (for example
  no power meter). `"hide"` hides the element instead.
- `color_by` changes the color based on the value, for example heart rate or
  power zones:

  ```json
  "color_by": { "zones": "hr", "colors": ["#8ecae6", "#90be6d", "#f9c74f", "#f8961e", "#f94144"] }
  ```

### `gauge`: bar or arc

```json
{ "type": "gauge", "field": "power", "shape": "arc",
  "min": 0, "max": "ftp*1.5",
  "track_color": "#ffffff30", "fill_color": "#ffb703", "thickness": 14,
  "at": { "x": -40, "y": -40, "anchor": "bottom-right" },
  "size": { "width": 200, "height": 200 } }
```

`shape` is `bar` (horizontal), `vbar` or `arc`. `min` and `max` are numbers,
`"auto"` (the field's range over the activity, from `Segment`'s
`computed_*_range`), or simple expressions on athlete settings such as `ftp`
and `max_hr`.

### `graph`: profile with a cursor

```json
{ "type": "graph", "field": "altitude", "x_axis": "distance",
  "window": "all",
  "line": { "color": "#ffffff", "width": 3 },
  "fill": "#ffffff30",
  "cursor": { "color": "#ffb703", "radius": 8 },
  "at": { "x": "50%", "y": -40, "anchor": "bottom" },
  "size": { "width": 900, "height": 120 } }
```

- `x_axis` is `distance` or `time`.
- `window: "all"` shows the whole activity with a cursor at the current
  position, which is the classic elevation profile. `window: 60` shows a
  rolling 60-second trace, which suits heart rate or power.

### `map`: GPS track, position, optional OSM underlay

```json
{ "type": "map",
  "at": { "x": -40, "y": 40, "anchor": "top-right" },
  "size": { "width": 420, "height": 420 },
  "clip": "circle",
  "border": { "color": "#ffffff", "width": 4 },
  "view": { "mode": "follow", "zoom": 15, "rotate": "heading" },
  "tiles": {
    "source": "osm",
    "opacity": 0.8,
    "filter": "grayscale",
    "attribution": "auto"
  },
  "track": {
    "done":      { "color": "#ffb703", "width": 6 },
    "remaining": { "color": "#ffffff80", "width": 4 },
    "outline":   { "color": "#00000080", "width": 2 }
  },
  "marker": { "shape": "dot", "color": "#ffffff", "radius": 10,
              "outline": "#ffb703" } }
```

- **`view.mode`**:
  - `fit`: the whole track fits in the box and doesn't move, and only the
    marker moves. This is what `Map::init(storage, box, w, h)` already does.
  - `follow`: the map is centered on the current position at a fixed `zoom`.
  - `rotate: "heading"`: the map turns so the direction of travel points up.
    This is optional and much more work (see below).
- **`clip`**: `rect`, `rounded` (uses `radius`) or `circle`.
- **`tiles`**: leave the key out or use `null` to show only the track with no
  underlay. `source` is `osm` (the current
  `https://tile.openstreetmap.org/{z}/{x}/{y}.png`) or a custom URL template.
  `filter` is `none`, `grayscale`, `dark` (inverted grayscale) or a tint color,
  to keep the map from competing with the footage.
- **`attribution`**: OSM's license requires "© OpenStreetMap contributors"
  wherever its tiles are shown, including in videos. `"auto"` draws it small
  inside the map box. It can be moved, but it shouldn't be possible to turn it
  off for the OSM source.

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

## Fields

These map onto `FIT::record`, `Lap` and `Segment`. Every field `f` also has
`f.unit` (the unit string for templates). Numeric fields also have `f.avg`
and `f.max`, computed from the start of the activity up to now, and
`f.lap_avg`, computed over the current lap.

| Field         | Source                         | Metric unit | Imperial unit |
|---------------|--------------------------------|-------------|---------------|
| `speed`       | `record.speed`                 | km/h        | mph           |
| `pace`        | derived from `speed`           | min/km      | min/mi        |
| `heart_rate`  | `record.heart_rate`            | bpm         | bpm           |
| `power`       | `record.power`                 | W           | W             |
| `power_wkg`   | `power` / athlete weight       | W/kg        | W/kg          |
| `cadence`     | `record.cadence`               | rpm / spm   | rpm / spm     |
| `altitude`    | `record.altitude`              | m           | ft            |
| `grade`       | `record.grade`                 | %           | %             |
| `distance`    | `record.distance`              | km          | mi            |
| `temperature` | `record.temperature`           | °C          | °F            |
| `lr_balance`  | `record.left_right_balance`    | %           | %             |
| `elapsed`     | timestamp − activity start     | h:mm:ss     | h:mm:ss       |
| `time_of_day` | `record.timestamp` (local)     | HH:MM       | HH:MM         |
| `lap`         | index of the current `Lap`     |             |               |
| `title`       | `Activity.title`               |             |               |
| `date`        | `Segment.start_time`           |             |               |

`unit` on an element is `"auto"` (from `units`) or an explicit unit such as
`"m/s"`.

### Sampling and smoothing

Records usually arrive at 1 Hz and the video runs at 30 or 60 fps, so a value
that changes once a second looks jumpy.

- Every element can set `"smooth": 3`, a trailing average over that many
  seconds. The default comes from the field: 3 s for power and none for
  distance.
- Numeric fields and the map marker are **interpolated** linearly between the
  two records around the current frame time. Displayed integers (heart rate,
  power) are rounded after the interpolation. `"interpolate": false` turns
  this off.
- Gaps (auto-pause, signal loss): if the nearest records are more than
  `max_gap` seconds apart (default 5), the field counts as missing.

## Rendering notes

- **Fonts** are resolved through `appearance.font_directories` from
  `settings.json`. Each distinct (font, scaled size) pair is loaded once with
  `LoadFontEx` at the exact pixel size. Scaling a small font up blurs it.
- **Shadow/outline**: draw the text again with an offset, or blur it in a
  shader. On top of arbitrary footage, text without a shadow or outline is hard
  to read, which is why `defaults` includes a shadow.
- **Map tiles in `follow` mode**: render the tiles for the whole track into one
  large off-screen texture once, at the chosen zoom, and on each frame draw
  the visible rectangle of it with `DrawTexturePro`. That turns per-frame tile
  handling into one blit. A ride at zoom 15 can still need a few hundred tiles,
  so:
  - Go through the existing tile cache (`Tile::get_tile`).
  - Respect the [OSM tile usage policy](https://operations.osmfoundation.org/policies/tiles/):
    send a valid User-Agent, no bulk prefetching beyond what's needed, and
    throttle downloads. For heavy use, allow a custom tile server or a
    commercial provider via the `source` URL template.
- **Rotation** (`rotate: "heading"`): draw the map texture rotated around the
  marker inside the clip region. Use a scissor rectangle for `rect`, or a
  stencil/shader mask for `circle`. Smooth the heading heavily, because GPS
  heading jitters at low speed.
- **Preview**: the same renderer should draw into the activity screen with a
  time scrubber, so layouts can be adjusted without encoding a video. Hot
  reload when the layout file changes would make editing layouts by hand
  bearable.

## Open questions

1. Comments in layout JSON (`//`)? Or JSON5?
2. Scale uniformly, or per axis, when the output size differs from
   `design_size`?
3. Should portrait videos (9:16) need their own layout, or should anchors
   handle them? Anchors probably cover most cases, but a map plus a profile
   along the bottom will be cramped.
4. Athlete settings (FTP, max HR, weight, zones): `JANS_FTP` is currently a
   constant. Zone and gauge expressions need a real athlete profile.
5. Should the layout name the units, or should that be an app setting that
   layouts inherit?
6. Should there be a graphical layout editor, or is editing JSON by hand with
   a live preview enough for now?
