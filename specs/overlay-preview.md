# Overlay Preview: Text and Values

Status: Draft

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

- **Ctrl+O**, the task `st-overlay-preview`, turns the preview on and off.
  With the preview on, the canvas takes the place of the data panel and the
  map. The lap table and the graph stay, so that clicking or dragging in the
  graph moves the moment the preview shows.
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
  Picking a layout turns the preview on.
- **Reloading**: when the layout file changes on disk, the preview reloads
  it, within a second, so that saving in an editor shows the result.
- **Errors**: a layout that doesn't parse, or has a property that's wrong
  (an unknown element type, a color that isn't one), isn't shown. The canvas
  shows the error, with the file and, where there is one, the element's
  index and the property, such as
  `cycling-basic.overlay.json: element 3: "colour" isn't a property of a
  value`. Fixing the file and saving it shows the layout again.

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
can be tried now.

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

1. Is Ctrl+O the right key, and should the preview replace the data panel
   and the map, or only the map?
2. Should the layout list (Ctrl+Shift+O) be in this slice, or is one
   built-in layout plus the user's `overlays/` directory, with the first
   found shown, enough for now?
3. Skipping elements of later slices rather than refusing the layout: OK?

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
