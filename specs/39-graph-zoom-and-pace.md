# Zooming the graph, and pace for runs

Status: Ready

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

Two improvements to the activity screen's graph that were deferred from
`specs/37-graph-improvements.md` and `specs/38-extend-selection.md`:

- **Zooming to the selection.** A selection, dragged, extended or a lap,
  fills the graph's width, so that a 2-minute interval in a 3-hour ride
  is more than a few pixels wide. Zooming out shows the whole activity
  again.
- **Pace for runs.** The graph draws speed in km/h for every sport, while
  runners think in pace, minutes per km, as the data panel already gives
  it for runs.

## Feature

### What there is now

- The graph's x is the record (spec 37): every place that maps between
  x and records divides the plot's width by the number of records. That
  covers the lines, the time and km labels, the shading of the selected
  range, the mark, dragging, and the lap strip and its clicks
  (`lap_spans()`).
- Spec 38 added a right-click menu to the graph's plot, `PopupMenu`
  built by `SweatTrails::selection_menu()`, whose items are tasks. This
  spec adds "Zoom to selection" and "Zoom out" to it.
- The map has its own zoom on `+`, `-` and `0`. Esc is "Show the whole
  activity", which clears the selection. `Z` isn't used.
- Speed is drawn from 0 to its highest smoothed value, in `SpeedColor`,
  as "Spd 0–15 km/h" in the legend and "Spd 12.4 km/h" while hovering.
- The data panel gives a run's pace with `format_pace()` in
  `DataFields.cpp`, as "5:02 min/km". The overlay's fields treat a pace
  slower than 60:00 min/km as absurd (`SlowestPace` in
  `overlay/Fields.cpp`), and the summaries' pace curve has `pace_line()`
  and `pace_ticks()` for a pace axis (spec 35).
- `is_on_foot()` in `DataFields.cpp` tells a run or walk from a ride.

### Zooming to the selection

- **Zooming in**: "Zoom to selection" in the right-click menu, or `Z`,
  makes the selection, dragged, extended or a lap, fill the graph's
  width. Zoomed in, `Z` zooms out again: it toggles. "Zoom out" in the
  menu does the same. Both are tasks with titles, so the command palette
  has them too.
- **Esc stays as it is**: it clears the selection, and leaves the zoom.
  Esc meaning "zoom out" at some times and "clear the selection" at
  others would surprise.
- **Zoomed in, everything works**: hovering, dragging a selection, the
  lap strip, and extending with the right-click menu, all within the
  zoomed part. You can zoom again, to a smaller selection. Zooming out
  always goes back to the whole activity, so there's no stack of zooms
  to step back through.
- **The zoom is the view, the selection is what's selected**: they're
  independent. Selecting another lap with Up or Down, or clearing the
  selection with Esc, leaves the zoom as it is, even if the new
  selection is outside it, as a selection on the map can be outside its
  view. Showing another activity, or another recording of it, zooms out.
- **The lines and the legend are the zoomed part's**: each line fills
  the plot's height over the zoomed part, and the legend gives that
  part's ranges. You zoom in to see detail, and that's where it shows.
- **Without a selection** "Zoom to selection" is greyed out in the menu,
  as an extension with nowhere to go is, and `Z` does nothing, except
  zoom out when zoomed in.
- **The labels count from the activity's start**, zoomed in too: zoomed
  to 1:12–1:18, the time labels say 1:13, 1:14, and the km labels 23 km,
  24 km. They say where in the activity you are, as the legend's time
  and distance at the mark do. Their steps are picked for the zoomed
  part's length, by the same rule as for the whole activity (spec 37):
  labels about 80 pixels apart. The time steps get 10 and 30 seconds
  below a minute, for zooms of a few minutes, labelled "1:12:30".
- **The lap strip** shows the laps over the zoomed part, those cut off
  at its edges cut off too. A click selects the lap under it, as now.
- **The smoothing stays 5 seconds**. Zoomed in to a few minutes, a
  record is a few pixels wide, and the average still takes out the
  1-second noise the zoom would otherwise show at full size.
- **The map doesn't zoom**. It already draws the selection, and `0` fits
  the whole route; zooming the graph is about the graph.

### Pace for runs

- **On foot, pace replaces speed**: runs, walks and hikes, as
  `is_on_foot()` says and as the data panel does, show pace in place of
  speed, one line in `SpeedColor`. Rides and other sports keep speed in
  km/h.
- **The line is the speed line**, faster higher, smoothed in the same
  way, with its scale and legend in pace. A pace axis that's linear in
  pace would squeeze the running into a sliver whenever there's walking,
  and a standstill is an infinite pace.
- **The scale runs from the fastest pace to the slowest**, but no slower
  than 20:00 min/km, 3 km/h. That's the line's bottom: anything slower,
  a walk up a steep hill or standing still, is drawn at the bottom. So
  the running fills the plot's height.
- **The legend** says "Pace 3:50–12:00 min/km", fastest first, as the
  line goes from top to bottom; "Pace 3:50–20:00 min/km" when the
  slowest is the limit.
- **While hovering** it says "Pace 5:02 min/km", the record's own, as
  recorded, in `format_pace()`'s form. A record standing still, slower
  than `RangeMovingSpeed`, says "Pace –".
- Zoomed in, the pace scale is the zoomed part's, as the other lines'
  are.

### Version 1 scope

In: zooming the graph to the selection and out, with `Z`, the
right-click menu and the palette, the lines, labels and strip following
the zoom; pace in place of speed on foot.

Deferred:

- A stack of zooms, zooming out one step at a time *(deferred)*.
- Zooming with the mouse wheel, or scrolling the zoomed graph sideways
  *(deferred)*.
- Pace in min/mi *(deferred)*, with the rest of miles.

## Open questions

None.

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
