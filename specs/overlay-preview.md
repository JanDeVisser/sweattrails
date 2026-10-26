# Overlay Preview: Text and Values

Status: Done

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
- Style properties: `font`, `font_size`, `color`, `shadow` (a color and an offset;
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
- **`list_box(target, prompt, entries, submit, initial)`** in `Modal.h`,
  next to `input_box()` and `query_box()`: a `PickBox`, a ListBox of texts
  with payloads, whose Enter calls `submit(target, payload)`, with the
  entry `initial` selected. The layout list is the first user; there was
  no helper for "pick one of these". `PickBox` is a named template, not a
  local class, so that a test can make one without opening it. The
  payloads have to compare, since ListBox sorts its entries: the layout
  list's are the layouts' names.

### The layout: `overlay/Layout.h`

- **`OverlayLayout`**: `design_size`, `units`, the elements, and how many
  were skipped. `decode_layout(JSONValue const &)` returns it or a
  **`LayoutError`** (`element` index if any, `property`, and a message),
  whose `to_string()` is what the canvas shows.
- Decoding is as in the [reference](overlay-layout.md#format-json): for
  each element, `defaults` merged with each of its styles and then its own
  properties, and the merged object decoded by the element type's decoder.
  The merge is `cascade(base, over)` here, not `JSONValue::merge()`, which
  appends arrays, as the settings want: a style's `"offset": [3, 3]` has to
  replace the default's `[2, 2]`, not be added to it.
- **Elements** are a `std::variant<TextElement, ValueElement>`. Both have a
  `Placement` (`at`, `anchor`, `size`) and a `TextStyle` (`font`, `font_size`,
  `color`, `shadow`); `TextElement` a `Template` and `align`;
  `ValueElement` a field, its format spec, its unit, a `unit_style` and
  `missing` (default `--`).
- **Defaults and styles are checked on their own**, for any element's
  properties, also later slices', and an element only for its own, so
  that a typo is reported where it is, and a text's `align` in the
  defaults isn't an error on a value.
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
- **`font_size`, not `size`, is the font's size**, as Jan decided: the
  reference used `size` for both the font and the element's box, and an
  element's box then replaced its style's font size in the cascade. The
  reference is updated.
- **A value's `format`** is `"{:spec}"`, as in the reference, and only
  that: one placeholder and nothing around it, whose spec is kept.
- **Formats** are checked when the layout loads, by formatting a number
  with them, so that a bad spec is a layout error, not a `std::format_error`
  in the middle of drawing. That's the one place that catches an
  exception: `std::vformat` has no other way to say a spec is wrong.

### Fields and templates: `overlay/Fields.h`, `overlay/Template.h`

- **`OverlayField`**, an X-macro enum: `speed`, `power`, `heart_rate`,
  `cadence`, `distance`, `elapsed`.
- **`field_value(Activity const &, size_t record, OverlayField)`**: the
  value as a `double`, in the field's base unit (m/s, m, s), or nothing.
  Cadence is as FIT records it; `spm`, the running default, doubles the
  strides of one foot to steps. `elapsed` is the record's
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

- **`LayoutFile`**: a path, the time it was last changed (none if it
  wasn't there), and the decoded layout or its error, which starts with
  the file's name. `reload_if_changed()` compares the file's time, and
  reloads; the preview calls it once a second, while it's shown.
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
  layout's texts, units and `missing` texts. The fonts are looked up
  once per load of the layout, with `SweatTrails::find_font()`, which
  `load_font()` now uses too; a font not in the font directories is an
  error on the canvas, with the element. When the scale changes, the
  cache is cleared.
- **The record shown** is `DisplayedActivity::current_record()`: the mark,
  or else the end of the range a drag in the graph is selecting, which
  clears the mark, or else the first.
- **Fonts** are found by `layout_fonts(layout, find)` in `LayoutFile.h`,
  out of the widget, so that a test reaches it; elements know their place
  in the file (`element_number()`), also after skipped ones, for the
  error. `FontCache` remembers a font it couldn't load, and the canvas
  says so.
- **In its box**, a text or a value sits where its anchor is
  (`place_in_box()`); a text's `align`, if given, picks the horizontal
  place. Errors on the canvas are wrapped with `wrap_words()` in
  `StringUtil`.
- **`ActivityDisplay`** gets the preview, `previewing()` and
  `show_preview()`, next to its `central` row (the
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

- `JSONTest`: comments in a document.
- `ColorsTest` (new): `#rrggbb`, `#rrggbbaa`, upper and lower case, and
  what isn't a color (no `#`, wrong length, not hex).
- `OverlayLayoutTest` (new): the cascade (defaults, styles in order, own
  properties, nested objects merged and arrays replaced; a style that
  doesn't exist is an error); decoding `text` and
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

1. **Bug: dragging in the graph shows the first record.**
   `widget/OverlayPreview.cpp:206` takes `mark.value_or(0)`, but a press in
   the graph clears the mark (`widget/Activity.cpp:354`) and the drag only
   sets `segment`. So while a range is dragged, the preview jumps to record
   0 (`elapsed` 0:00:00) and stays there if the button is let go outside the
   graph. The spec says dragging moves the moment shown. Fix: fall back to
   `segment->max` (the drag's end) before record 0, in a small
   `preview_record(DisplayedActivity const &)` that a test can reach.
2. **Bug: a missing font's error gives the wrong element.**
   `widget/OverlayPreview.cpp:85-88` counts `layout->elements`, which leaves
   out the skipped elements of later slices. With `[ map, text with "font":
   "Nope.ttf" ]` the canvas says `element 1`, but it's element 2. Fix: keep
   each element's index in the file in `OverlayLayout` (or in the
   elements), and use that.
3. **Bug: a font that's found but can't be loaded disappears silently, and is
   loaded again on every frame.** `FontCache::get()`
   (`Lib/Widget/FontCache.cpp:32-36`) doesn't remember a failure, so
   `draw_element()` (`widget/OverlayPreview.cpp:210`, `:231`) calls
   `LoadFontEx()` again on every frame, which reads the file each time, and
   the element isn't drawn and no error is shown. This happens, for example,
   with a corrupt file, or a `.ttc` from `/System/Library/Fonts`, which
   `find_font()` finds but raylib doesn't load as a TrueType font. Fix: cache
   the failure in `FontCache`, and show it on the canvas like a font that
   isn't found, with the element.
4. **Bug (test fails): a running cadence in `"unit": "rpm"` is doubled.**
   `field_value()` (`overlay/Fields.cpp:169-175`) doubles running cadence to
   steps, and both `rpm` and `spm` have factor 1 (`Fields.cpp:39-40`,
   `:120`). So a value with `"field": "cadence", "unit": "rpm"` on a run
   shows 176 rpm for 88 strides per minute. Fix: have `field_value()` give
   the FIT value (strides or revolutions), with `spm` a factor 2 unit and
   the running default; or reject `rpm` for a run. Test:
   `test_running_cadence_in_rpm` in `OverlayFieldsTest.cpp`.
5. **Bug (test fails): `unit_style`'s shadow doesn't merge.**
   `text_style()` (`overlay/Layout.cpp:231`) builds a new `Shadow` for every
   `shadow` object. The JSON cascade has merged it already for the element's
   own style, but `unit_style` is applied over the value's style later
   (`Layout.cpp:328`) without that merge. So `"unit_style": { "shadow": {
   "offset": [1, 1] } }` turns the default `#000000a0` shadow into opaque
   black. Fix: start from `base.shadow` when there is one, or
   `cascade()` the value's style JSON with `unit_style` before decoding it.
   Test: `test_unit_style_shadow_merges` in `OverlayLayoutTest.cpp`.
6. **Suggestion: check `defaults` and `styles` on their own.** Properties are
   checked on the merged element (`Layout.cpp:246`, `:295`), so a typo
   `"colour"` in `defaults` is reported as `element 1: "colour" isn't a
   property of a text`, which sends the user looking in element 1. And a
   property of one element type in `defaults`, such as `"align"` or
   `"missing"`, makes every element of the other type an error. Checking
   `defaults` and each style against all the element and style properties,
   with `defaults` or `styles.label` in the message, would point at the
   right place.
7. **Suggestion: an explicit `size` on a value is only half used.**
   `widget/OverlayPreview.cpp:246-251` places the box with `size`, but draws
   the number at the box's top, from the measured height. With `"anchor":
   "bottom-left"` and `"size": { "height": 120 }`, a 64 px number floats at
   the top of the box, not at the anchor's bottom. A text's box has no
   vertical alignment either. Fix: put the content at the box's bottom (or
   anchor it within the box), or leave `size` out until groups need it.
8. **Suggestion: the error message on the canvas is one line, clipped.**
   `draw_message()` (`widget/OverlayPreview.cpp:168-179`) draws the error
   on one line inside the scissor. A font error such as
   `cycling-basic.overlay.json: element 3: the font `X` isn't in any of the
   font directories` is cut off on a canvas that's narrower than the text,
   which happens in a small window. Wrap it to the canvas's width.
9. **Missing test: what the preview shows is only reachable through
   `draw()`.** Which record it shows, the font lookup and its error, and the
   value/unit layout are all inside `OverlayPreview`, so 1 and 2 couldn't be
   tested. Pulling `preview_record()` and the font check (given a
   `find_font` function) out as plain functions would make them testable,
   as the spec's architecture intends ("everything up to drawing is plain
   logic").

Tests added by the Reviewer: in `OverlayLayoutTest.cpp`, the element index
after skipped elements, typos in defaults and styles, the cascade through
`decode_layout()`, coordinates and percentages, `place()` on a scaled canvas,
value formats, sizes, codepoints, and the `unit_style` shadow (fails, 5). In
`OverlayFieldsTest.cpp`, running cadence in rpm (fails, 4), `elapsed` edge
cases, no records, template braces and specs, and specs that would throw.
In `LayoutFileTest.cpp`, a file made after it was looked for, the error's
wording as in the spec, missing directories, a state file without a name,
and the built-in layout's unit fonts. In `ColorsTest.cpp`, signs, prefixes
and spaces. `std::vformat` can't throw at draw time: every spec that reaches
`format_field()` has gone through `is_valid_spec()` with the same argument
type, and `elapsed` never reaches `std::vformat`.

### Resolutions

All fixed, as Jan agreed, each in its own commit:

1. `c10b680`: `DisplayedActivity::current_record()`, the mark, else the
   drag's end, else the first record; `test_current_record`.
2. `c35d6e5`: elements keep their place in the file, and `layout_fonts()`,
   now outside the widget, names it; `test_layout_fonts`.
3. `c13597e`: `FontCache` remembers a failure until it's cleared, and the
   canvas names the element whose font can't be loaded. Checked by
   reading: loading fonts needs raylib.
4. `092b00a`: `field_value()` gives cadence as FIT records it, and `spm`
   has a factor of 2; `test_running_cadence_in_rpm` passes.
5. `0d1a9f5`: a shadow starts from the one it's applied over;
   `test_unit_style_shadow_merges` passes.
6. `8af27cd`: defaults and styles are checked on their own, and elements
   only for their own properties; the checks in
   `test_typos_in_defaults_and_styles` name the defaults or the style.
7. `df77f3b`: `place_in_box()` puts the content where the anchor is in the
   box, and a text's `align` is optional; the reference says so too;
   `test_place_in_box`.
8. `8db8096`: `wrap_words()` in `StringUtil`; `test_wrap_words`.
9. With 1 and 2: `current_record()` and `layout_fonts()` are tested
   without a window.
