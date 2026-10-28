# Overlay visibility: showing elements for part of a video

Status: Done

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
  moment, unless the range is changed in the render's box. A mark at the
  range's very end, the selection's last record, is the video's last
  frame, so that what shows to the end shows there. Measuring
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
the start or both from the end, such as `{ "from": 6, "to": 2 }`; and
a `to` of 0, which ends at the video's start, and was most likely meant
as `"end"`. A
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
- `visibility_alpha()`: from `from` up to, not including, `to`, both cut
  to the video, with the fades inside:
  `min(1, (t - from) / fade, (to - t) / fade)`.
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
  now, and `preview_time(activity, elapsed, selection)` too, for the
  preview: `at` is the marked record's elapsed time less the range's
  start, and the range's end is the video's last frame, at 30 fps. `DisplayedActivity::selection()` gives it the
  graph's selection as a pair of records. A mark outside the range shows only the
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
- `OverlayRenderTest`:
  `initial_render_range()` with a selection, a selection of one record,
  and none.
- `OverlayTilesNeededTest`: no tiles for a hidden map.
- `LayoutFileTest`: the dashboard's title card.
- By hand, with a throwaway frame dump: the title card fading in and out
  at the start of a render, a `from` from the end, and the preview with
  a selection.

## Review notes

1. **Bug**: a window that reaches past the video's start or end doesn't
   fade there; the element pops in or out at full opacity.
   `visibility_alpha()` (`Layout.cpp:1063`) measures the fades from
   `from` and `to` as given, not from where the video starts and ends,
   though the spec says that "at the start and the end of the video, too,
   the element fades in and out within the video". Three cases. The
   dashboard's title card (`"to": 5`, `"fade": 0.5`) in a render of 3 s
   is still at 1.0 on the last frame and is cut off. An outro
   `{ "from": -8 }` with `"fade": 1` in a render of 5 s is at 1.0 on
   frame 0. `{ "from": 1, "to": 10 }` in 5 s doesn't fade out. Clamping
   `from` to 0 and `to` to `length` before working out the fades would
   fix it. `test_visibility_alpha_edges` in `OverlayLayoutTest` has 4
   checks that fail on this.
2. **Suggestion**: `{ "from": -5, "to": 0 }`, or any `"to": 0` with a
   negative `from`, is empty for every video, since `at` is never below
   0, but it isn't an error. The check at `Layout.cpp:987` counts 0 as
   "from the start" and so lets the pair through. Treating `"to": 0` as
   always empty would follow the spec's rule that a window empty for
   every range is an error.
3. **Suggestion**: when you select a lap or drag a range and don't mark
   a record, the preview shows the selection's last record, so `at` is
   `length` exactly. As `to` isn't included, every element whose window
   runs to the end is hidden there: an outro, or an element with only
   `fade`. A render does show those elements on its last frames, at full
   opacity without a fade. This is the first thing the preview shows
   after you select a lap, so an outro looks broken. One fix is to show
   the preview at `length` as it would look just before.
4. **Missing test / suggestion**: the preview's `VideoTime` is worked
   out inline in `OverlayPreview::draw()` (`OverlayPreview.cpp:125`), so
   it's only checked by hand. Moving it to `overlay/Render` beside
   `initial_render_range()`, as something like
   `preview_time(activity, record, selection)`, would make it testable:
   a mark inside the selection, a mark before it, no selection, and the
   end of the selection from item 3.
5. **Suggestion**: the title card's panel is a fixed 1000 x 200. At 56 px
   Inter Bold, a ride title of more than about 30 characters runs past
   its edges, and Garmin and Strava titles are often that long. Until
   `group` (7c) can size a panel to fit its contents, a wider panel or a
   smaller title font would leave more room.

Checked and found right: every raylib draw call and color in
`Renderer.cpp` goes through `faded()`. `draw_line()` is given faded
colors, the attribution and the value's threshold color go through
`draw_text()`, and the tiles' tint is applied by `ClipShader` because it
multiplies by `fragColor`. `m_alpha` is set to 1 again after the loop.
In a render, `VideoTime` is the frame's time less the range's start. In
the preview, `elapsed` is on the same clock as `records_range()`.
`selection()` gives a lap's last record, not the record after it. The
cascade merges `visible` objects key by key.

Tests added (not committed): `test_visible_cascade` (an element's
`"end"` over a style's `to`, the defaults' window with an element's own
fade, a window that's only empty once merged, a style's error reported
for the element, and `visible` on gauge, graph and profile) and
`test_visibility_alpha_edges` in `test/OverlayLayoutTest.cpp`, and
`test_selection` in `test/DisplayedActivityTest.cpp`. The full run passes
except `overlay_layout_test`, which fails on item 1.

### Resolution

1. Fixed: the window is cut to the video before the fades are worked
   out. `test_visibility_alpha_edges` fails on the old code.
2. Fixed: a `to` of 0 is an error, which says the end is `"end"`.
3. and 4. Fixed together: `preview_time()` in `overlay/Render` works
   out the preview's time, and takes the range's very end for the
   video's last frame. `test_preview_time`.
5. Fixed: the title card's panel is 1400 px wide, with the title at
   48 px, for some 50 characters.

The review's tests are committed.
