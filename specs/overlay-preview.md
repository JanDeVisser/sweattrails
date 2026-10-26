# Overlay Preview: Text and Values

Status: In progress

The first slice of the video overlay (see the
[roadmap](overlay-layout.md#roadmap)). The format is described in
[overlay-layout.md](overlay-layout.md), the design reference; this spec says
which parts of it are built now, and how the preview works in the app.

## Goal

The user can write an overlay layout, a JSON file that says which data is
shown where, in which font, size and color, and see it in the app over the
activity on display, at the moment marked in the graph. Saving the file
updates the preview, so that a layout can be made by hand, which is the only
way to make one (there's no layout editor).

This slice draws text and values only, and doesn't render video yet: that's
the next slice. It builds the base the other elements stand on: the layout
file, the style cascade, positioning, colors, and fonts at the right size.

## Feature

### The preview

- **Ctrl+O**, the task `st-overlay-preview`, turns the preview on and off:
  Ctrl and a letter, like the app's other keys. With the preview on, the
  canvas takes the place of the data panel and the map, both rather than
  only the map, so that it's large enough to judge font sizes by. The lap
  table and the graph stay, so that clicking or dragging in the graph moves
  the moment the preview shows.
- The canvas has the aspect ratio of the layout's `design_size`, as large as
  fits, centered. Its background is a dark checkerboard, the usual way to
  show that the overlay is transparent there.
- The preview shows the activity at the mark (`DisplayedActivity::mark`), or
  at its first record if nothing is marked. There's no interpolation between
  records yet: that comes with rendering to video, where frames fall between
  records.
- Without an activity on display, Ctrl+O does nothing, like the other keys,
  and nothing under a modal.
- **The layout** shown is the one picked last. **Ctrl+Shift+O**, the task
  `st-overlay-layout`, opens a list of the layouts: the built-in ones in
  `share/overlays/`, and the user's in `overlays/` in the app's data
  directory (`~/Library/Application Support/sweattrails/overlays/` on
  macOS). A user's layout with the same name as a built-in one comes first.
  The choice is kept in the app state, so it's there after a restart.
  Picking a layout turns the preview on. Before a layout has been picked,
  the preview shows the built-in `cycling-basic`.
- **Reloading**: when the layout file changes on disk, the preview reloads
  it, within a second, so that saving in an editor shows the result.
- **Errors**: a layout that doesn't parse, or has a property that's wrong
  (an unknown element type, a color that isn't one), isn't shown. The canvas
  shows the error, with the file and, where there is one, the element's
  index and the property, such as
  `cycling-basic.overlay.json: element 3: "colour" isn't a property of a
  value`. A font that isn't in any of the font directories is an error too.
  Fixing the file and saving it shows the layout again.

### What's built of the layout format

From [Layout structure](overlay-layout.md#layout-structure):

- `version` (1), `design_size`, `units`, `defaults`, `styles`, `elements`.
- The style cascade: `defaults`, then each style named in `style` (a string
  or an array), then the element's own properties; later wins, and objects
  merge.
- Positioning: `at` with `x` and `y` as design pixels or percentages,
  negative from the right or bottom, and `anchor`; `size`. Everything scales
  with the canvas, by `min(canvas_w / design_w, canvas_h / design_h)`.
- Colors: `#rrggbb` and `#rrggbbaa`.
- Comments, `//` and `/* */`, in the layout file.
- Style properties: `font`, `size`, `color`, `shadow` (a color and an offset;
  a hard shadow, not blurred).

The elements, from [Element types](overlay-layout.md#element-types):

- **`text`**, with `text` and `align`. Placeholders `{field}`,
  `{field:spec}` and `{field.unit}` are filled in, with `std::format` specs
  for numbers. The duration and date specs (`hms`, `ms`, `hm`, `%d %b %Y`)
  come with the other fields, in slice 3; until then `elapsed` is always
  `h:mm:ss`.
- **`value`**, with `field`, `format`, `unit` (`"auto"` or an explicit
  unit), `unit_style` and `missing` (a text, or `"hide"`). `color_by` comes
  in slice 3.

The other elements (`gauge`, `graph`, `profile`, `map`, `image`, `group`)
and `visible`/`fade` come in later slices. A layout that has them isn't an
error: they're skipped, and the canvas says once, in a corner, how many
elements it couldn't show yet, so that a layout written for a later slice
can be tried now, and fills in slice by slice. Their properties aren't
checked until their slice.

### Fields

`speed`, `power`, `heart_rate`, `cadence`, `distance` and `elapsed`, as in
[Fields](overlay-layout.md#fields), in metric and imperial units. A field
the activity doesn't have, or that the record at the mark doesn't have, is
missing. The pause rules and the other fields come in slice 3.

### Fonts

- Fonts are found as now, through `appearance.font_directories` in
  `settings.json`.
- Each font at each size it's drawn at is loaded once, at the exact pixel
  size the canvas needs, so that text is sharp; when the canvas changes size,
  the fonts for the new size are loaded. Only ASCII, and the characters the
  layout's texts use, are loaded.
- **Inter** (SIL Open Font License), Regular and Bold, is shipped in
  `share/fonts`, for the built-in layout. The README lists its license.

### The built-in layout

`share/overlays/cycling-basic.overlay.json`: speed and power in the bottom
left, each a `value` with a small label above it, heart rate and cadence
next to them, and the distance and elapsed time in the top left, at 1920 ×
1080. It doubles as the example layout to copy.

### Version 1 scope

Everything above. Deferred to later slices of the
[roadmap](overlay-layout.md#roadmap): rendering to video and interpolation,
the other fields and specs, `color_by`, the other elements, and
`visible`/`fade`.

## Open questions

None.

## Architecture

The overlay's code goes in a new directory, `src/App/overlay/`, added to the
App include group in `.clang-format`. Everything up to drawing is plain
logic, without raylib calls, so that the tests reach it; drawing is one
thin file.

### In the libraries

- **`JSONValue::merge(JSONValue const &over)`** in `JSON.h`: `over`'s keys
  replace this object's, and where both are objects, they merge,
  recursively. This is the style cascade.
- **Comments** already work: the JSON lexer skips `//` and `/* */` unless
  it's asked to keep them (checked with a scratch build). A test in
  `JSONTest` makes sure it stays that way.
- **`parse_color(std::string_view)`** in a new `src/Lib/Widget/Colors.h`:
  `#rrggbb` and `#rrggbbaa` to a raylib `Color`, or nothing. In the Widget
  library, since the themes need it too (their colors are read but never
  parsed). `Colors.h`, not `Color.h`, so it's not taken for raylib's type.
- **`FontCache`** in a new `src/Lib/Widget/FontCache.h`: a font file at a
  pixel size, with a set of codepoints, loaded once with `LoadFontEx`, and
  unloaded with the cache. `clear()` drops them all, for when the canvas
  changes size.
- **`list_box(target, prompt, entries, submit)`** in `Modal.h`, next to
  `input_box()` and `query_box()`: a ListBox of strings with a payload,
  whose Enter calls `submit(target, payload)`. The layout list is the first
  user; there was no helper for "pick one of these".

### The layout: `overlay/Layout.h`

- **`OverlayLayout`**: `design_size`, `units`, the elements, and how many
  were skipped. `decode_layout(JSONValue const &)` returns it or a
  **`LayoutError`** (`element` index if any, `property`, and a message),
  whose `to_string()` is what the canvas shows.
- Decoding is as in the [reference](overlay-layout.md#format-json): for
  each element, `defaults` merged with each of its styles and then its own
  properties, and the merged object decoded by the element type's decoder.
- **Elements** are a `std::variant<TextElement, ValueElement>`. Both have a
  `Placement` (`at`, `anchor`, `size`) and a `TextStyle` (`font`, `size`,
  `color`, `shadow`); `TextElement` a `Template` and `align`;
  `ValueElement` a field, its format spec, its unit, a `unit_style` and
  `missing` (default `--`).
- **Unknown properties are errors, properties of later slices aren't.**
  Each element type knows all the properties the reference gives it, and
  all style properties, including those of later slices, such as
  `background` and `padding`, since `defaults` and styles may hold them.
  A property that's none of these is a typo, and an error. An element whose
  `type` is one of a later slice is counted in `skipped`; an unknown `type`
  is an error.
- **Placement** (`place()`, pure): the element's box, given its size
  (`size`, or the measured text), the canvas, and the scale. One rule for
  `x` and `y`, which fits every example in the reference: **the sign picks
  the edge**, positive from the left or top, negative from the right or
  bottom, and percentages likewise of the width or height; **`anchor` is
  the element's point that's put there**. So `bottom-right` with `-40,
  -40` puts the element's bottom right corner 40 in from the canvas's, and
  `bottom` with `"50%", -40` centers it 40 up from the bottom. The
  reference's "also the canvas corner the coordinates are measured from"
  goes: under it, `"50%"` with `bottom` would be measured from the middle.
  The reference is updated to say this.
- **Formats** are checked when the layout loads, by formatting a number
  with them, so that a bad spec is a layout error, not a `std::format_error`
  in the middle of drawing. That's the one place that catches an
  exception: `std::vformat` has no other way to say a spec is wrong.

### Fields and templates: `overlay/Fields.h`, `overlay/Template.h`

- **`OverlayField`**, an X-macro enum: `speed`, `power`, `heart_rate`,
  `cadence`, `distance`, `elapsed`.
- **`field_value(Activity const &, size_t record, OverlayField)`**: the
  value as a `double`, in the field's base unit (m/s, m, s), or nothing.
  Running cadence is doubled to steps per minute. `elapsed` is the record's
  time since the activity's start.
- **Units**: per field, the units it can be shown in, with their factors
  (`km/h`, `mph`, `m/s` for speed; `km`, `mi`, `m` for distance), the
  default for `metric` and `imperial`, and the default spec (`.1f` for
  speed and distance, `.0f` for the rest). An explicit unit the field
  doesn't have is a layout error.
- **`format_field(...)`**: the shown text: the value converted and
  formatted, `h:mm:ss` for `elapsed`, or the element's `missing`.
- **`Template`**: `parse_template()` splits a `text` into literal parts and
  placeholders, `{field}`, `{field:spec}` and `{field.unit}`, with `{{`
  and `}}` for braces. An unknown field is an error. A field of a later
  slice, such as `{lap}`, isn't: it shows as missing, in the spirit of
  skipping later elements.

### The file: `overlay/LayoutFile.h`

- **`LayoutFile`**: a path, the time it was last changed, and the decoded
  layout or its error. `reload_if_changed()` compares the file's time, and
  reloads; the preview calls it once a second.
- **`overlay_layouts(builtin_dir, user_dir)`**: the layouts in both
  directories, by name (the file name without `.overlay.json`), the user's
  winning over a built-in one of the same name, sorted by name.
- **The chosen layout** is kept in `overlay.json` in the user state
  directory, next to the app state, as `{ "layout": "cycling-basic" }`.
  Not in `AppState`, which is a fixed array of ints written as raw bytes;
  turning that into JSON would be its own change.

### Drawing: `widget/OverlayPreview.h`

- **`OverlayPreview`**, a widget: the checkerboard, the canvas, and the
  elements, each measured, placed with `place()`, and drawn with its shadow
  first; or the error. Fonts come from a `FontCache`, at
  `round(size × scale)` pixels, with ASCII and the characters in the
  layout's texts, units and `missing` texts; a font not in the font
  directories is a `LayoutError`. When the scale changes, the cache is
  cleared.
- **`ActivityDisplay`** gets the preview next to its `central` row (the
  data panel and the map), hidden; turning the preview on hides `central`
  and shows the preview, so the map keeps its own Hide/Relative policy.
- **Tasks** in SweatTrails.cpp: `st-overlay-preview` (Ctrl+O) toggles;
  `st-overlay-layout` (Ctrl+Shift+O) opens `list_box()` with
  `overlay_layouts()`, saves the choice, and turns the preview on. Both
  do nothing under a modal or without an activity, through
  `activity_for_keys()`.

### Files in `share/`

- `share/overlays/cycling-basic.overlay.json`.
- `share/fonts/Inter-Regular.ttf`, `Inter-Bold.ttf` and `Inter-OFL.txt`,
  from Inter's GitHub release. The install step already copies all of
  `share/`. The README lists Inter and its license.

## Test plan

- `JSONTest`: `merge()`, of nested objects, and of an object over a
  non-object; comments in a document.
- `ColorsTest` (new): `#rrggbb`, `#rrggbbaa`, upper and lower case, and
  what isn't a color (no `#`, wrong length, not hex).
- `OverlayLayoutTest` (new): the cascade (defaults, styles in order, own
  properties; a style that doesn't exist is an error); decoding `text` and
  `value`; the errors, with element index and property: unknown property,
  unknown type, bad color, bad format spec, unknown field, unknown unit;
  later slices' elements skipped and counted, and their style properties
  accepted; `place()` for every anchor, negative and percentage
  coordinates, and scaling.
- `OverlayFieldsTest` (new): each field from records made in the test, in
  metric and imperial and explicit units; missing fields; running cadence;
  `elapsed` as `h:mm:ss`; default specs; templates, with literal text,
  specs, `.unit`, escaped braces, a later slice's field, and errors.
- `LayoutFileTest` (new), in a temporary directory: loading, reloading
  after a change, an error and recovering from it; `overlay_layouts()`
  with a user layout over a built-in one; the chosen layout saved and read
  back.
- The built-in layout loads without errors (a test reads
  `share/overlays/cycling-basic.overlay.json`).
- By hand: Ctrl+O and Ctrl+Shift+O, the preview following the mark,
  editing a layout and seeing it reload, an error and fixing it, the
  window resized (sharp text at any size), and a layout with a `map`
  element.

## Review notes

*Filled in by the Reviewer.*
