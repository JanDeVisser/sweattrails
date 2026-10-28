# Overlay visibility: showing elements for part of a video

Status: In progress

## Goal

An overlay element can show for part of the video only, and fade in and
out: a title card with the ride's name and date for the first seconds, an
outro card for the last ones. Until now every element shows for the whole
video, so a title would have to be cut in the editor.

This is slice 7a of the [overlay roadmap](overlay-layout.md#roadmap). Slice
7 was split in three, as none of its parts needs the others: 7a is
`visible` and `fade`, the smallest, and new to what an overlay can do; 7b
is `image`, for icons beside values; 7c is `group`, which mostly tidies
layouts that already work, such as the dashboard's panels, which are
empty `text` elements now. The format is in the layout reference, under
[Visibility over time](overlay-layout.md#visibility-over-time). This spec
says what of it is built now, and where it departs from the reference.

## Feature

```json
{ "type": "text", "text": "{title}", "style": "title",
  "visible": { "from": 0, "to": 6 }, "fade": 0.5 }

{ "type": "text", "text": "Thanks for watching",
  "visible": { "from": -8 }, "fade": 1 }
```

- **`visible`**: `from` and `to`, in seconds of the video: from the start
  of the rendered range, or, if negative, back from its end, so that an
  outro card fits every video without editing the layout. `to` can also
  be `"end"`, as in the reference. `from` is 0 and `to` the end if left
  out. Any element can have it.
- **What the preview measures from**: it has no rendered range, only the
  moment marked in the graph. It takes the range a render would start
  with: the graph's selection, or else the lap, or else the whole
  activity. So the preview shows what the video would show at that
  moment, unless the range is changed in the render's box. Measuring
  from the activity's start instead would show a title card only in the
  ride's first seconds.
- **`fade`**: seconds, 0 (the default) for none. The fades are inside
  the window: the element starts fading in at `from`, and is gone at
  `to`, so that `from` and `to` say when it's on the screen; `"to": 6`
  isn't still there at 6.4 s. A window shorter than two fades doesn't get
  to full opacity, which suits a short flash. At the start and the end of
  the video, too, the element fades in and out within the video.
- `visible` and `fade` cascade like other properties: a `title` style
  can have them, and an element its own `to` over the style's window.
- The whole element fades: its panel, its text and shadow, a value's
  threshold color, a gauge's track and fill, a map's tiles, track,
  marker and attribution.
- Parts of an element that overlap fade each on their own. Halfway
  through a fade, a text's hard shadow shows through the text, and a
  map's panel through its tiles. raylib can't draw the element into a
  texture of its own and fade that, since a frame is drawn into one
  already and texture modes don't nest. It lasts as long as the fade;
  doing it properly needs a pass of its own, as the deferred blurred
  shadows do.
- An element that isn't showing isn't worked out either: a map that's
  hidden asks for no tiles.

### Errors

`visible` is an object with only `from` and `to`; `from` is a number,
`to` a number or `"end"`; `fade` a number of 0 or more. A window that's
empty for every range is an error: `from` not before `to`, both from
the start or both from the end, such as `{ "from": 6, "to": 2 }`. A
window that's only empty for some ranges isn't: `{ "from": 10 }` in a
video of 5 s just doesn't show, as the layout can't know how long the
video will be.

### Built-in layout

The `cycling-dashboard` gets a title card: the ride's `{title}` and its
`{date}` under it, centred in the frame on a panel, for the first 5
seconds, fading over 0.5 s. It shows what the feature is for, and gives
the Developer and the Reviewer something to check it with.

### Version 1 scope

In: `visible` with `from` and `to`, from the start or the end, and
`"end"`; `fade`; both in the cascade; the preview measuring from the
range a render starts with; the dashboard's title card.

*(Deferred)*:

- `"when": "lap_change"`, as in the reference.
- Fading in and out at different speeds, such as
  `"fade": { "in": 0.5, "out": 2 }`.
- Times as `"1:30"` rather than seconds.
- Fading an element as a whole, without its parts showing through each
  other.

## Open questions

None.

## Architecture

`visible` and `fade` are decoded in `overlay/Layout` for every element,
the alpha at a moment of the video is a pure function there, and
`overlay/Renderer` skips or fades each element by it. `visible`, `fade`
and `when` are already taken and ignored (`ElementProperties`).

### The layout

```cpp
// When an element shows, in seconds of the video: from its start, or if
// negative, back from its end. No `to` is the end.
struct Visibility {
    double                from { 0 };
    std::optional<double> to { };
    double                fade { 0 };
};

// Where a frame is in the video, and how long the video is, in seconds.
struct VideoTime {
    double at { 0 };
    double length { 0 };
};

// 0 when hidden, 1 when shown, and in between while it fades. An element
// without a Visibility always shows.
float visibility_alpha(std::optional<Visibility> const &visible, VideoTime time);
```

- Every element type gets `std::optional<Visibility> visible`: none
  when the cascaded element has neither `visible` nor `fade`. One
  decoder, `visibility(json)`, called once in `element()` on the
  cascaded properties and set with `std::visit`, rather than in each
  type's decoder.
- An element with `fade` only has the whole video for a window, so it
  fades in and out at its ends, as the spec has it.
- `visibility_alpha()`: from `from` up to, not including, `to`, with the
  fades inside: `min(1, (t - from) / fade, (to - t) / fade)`.
- `when` stays taken and ignored.

### Drawing

- `OverlayRenderer::draw()` and `tiles_needed()` take a `VideoTime`.
  An element at alpha 0 isn't drawn, and a map at 0 needs no tiles.
- Otherwise the renderer keeps the element's alpha in `m_alpha`, and
  every color it draws with goes through `faded(Color)`, which
  multiplies its alpha: the 21 raylib calls in `Renderer.cpp`, the text
  and its shadow in `draw_text()`, and the tiles' tint. There's no
  global alpha in raylib, and a shader can't do it either, as the map
  already draws in a shader of its own.

### The time

- **A render**: `at` is the frame's time less the range's start, and
  `length` the range's.
- **The preview**: `initial_render_range(activity, selection)`, in
  `overlay/Render`, is the range a render starts its box with: the
  selection, which is the lap when a lap is selected, if it's more than
  one record, or else the whole activity. `cmd_overlay_render()` uses it
  now, and the preview too, with `at` the marked record's elapsed time
  less the range's start. A mark outside the range shows only the
  elements that always show.

### Built-in layout

A `title_card` style in `cycling-dashboard` with `"visible": { "to": 5
}` and `"fade": 0.5`, for a panel and the `{title}` and `{date}` texts
on it, centred in the frame.

## Test plan

- `OverlayLayoutTest`: `visible` and `fade` decoded, with `"end"`,
  negative times and the defaults; in a style and the defaults, and an
  element's own `to` over a style's `from`; none without either; the
  errors: not an object, another property, a `from` or `to` that isn't
  a number, a `fade` under 0, and windows that are always empty.
- `OverlayLayoutTest`: `visibility_alpha()` before, at and after `from`
  and `to`, through both fades, from the end, a window shorter than two
  fades, `fade` alone over the whole video, and none.
- `OverlayRenderTest` (or where `records_range()` is tested):
  `initial_render_range()` with a selection, a selection of one record,
  and none.
- `OverlayTilesNeededTest`: no tiles for a hidden map.
- `LayoutFileTest`: the dashboard's title card.
- By hand, with a throwaway frame dump: the title card fading in and out
  at the start of a render, a `from` from the end, and the preview with
  a selection.

## Review notes

*Filled in by the Reviewer.*
