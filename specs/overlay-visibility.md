# Overlay visibility: showing elements for part of a video

Status: Ready

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

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
