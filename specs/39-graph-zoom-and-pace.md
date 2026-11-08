# Zooming the graph, and pace for runs

Status: Draft

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

This is still a stub: the two features are recorded here, and their
questions are to be settled with Jan before the spec is Ready.

## Feature

### What there is now

- The graph's x is the record (spec 37): every place that maps between
  x and records divides the plot's width by the number of records. That
  covers the lines, the time and km labels, the shading of the selected
  range, the mark, dragging, and the lap strip and its clicks
  (`lap_spans()`).
- Spec 38 adds a right-click menu to the graph's plot, to which this spec
  adds "Zoom to selection" and "Zoom out".
- Speed is drawn from 0 to its highest smoothed value, in `SpeedColor`,
  as "Spd 0–15 km/h" in the legend and "Spd 12.4 km/h" while hovering.
- The data panel gives a run's pace with `format_pace()` in
  `DataFields.cpp`, as "5:02 min/km". The overlay's fields treat a pace
  slower than 60:00 min/km as absurd (`SlowestPace` in
  `overlay/Fields.cpp`), and the summaries' pace curve has `pace_line()`
  and `pace_ticks()` for a pace axis (spec 35).
- `is_on_foot()` in `DataFields.cpp` tells a run or walk from a ride.

### Zooming to the selection

To be decided.

### Pace for runs

To be decided.

### Version 1 scope

To be decided.

## Open questions

Zooming:

- How to zoom in and out: the right-click menu's items, keys (such as
  Z, and Esc to zoom out), or both?
- Can a selection be dragged or extended while zoomed in, and can the
  graph zoom further in on a selection within the zoom?
- What happens to the zoom when the selection changes, such as with
  Up/Down to another lap, or Esc to the whole activity?
- Do the time and km labels count from the activity's start or from the
  zoom's left edge? How does the lap strip show laps cut off by the
  zoom's edges?
- Do the scales and the legend's ranges stay the whole activity's, so
  that the lines keep their height, or become the zoomed part's?
- Is the 5-second smoothing still right when a few minutes fill the
  graph, or should it be less when zoomed in?
- Does the map zoom to the selection too?

Pace:

- Does pace replace speed for runs and walks, or come as well?
- Which way is up: faster pace higher, as speed is, with the scale
  upside down?
- How slow a pace is drawn: a standstill is an infinite pace. Clip it,
  such as at 15:00 min/km, or leave a gap, as a dropout is?
- The legend's text: "Pace 4:10–6:30 /km", and "Pace 5:02 /km" while
  hovering?
- Which sports: runs, walks and hikes, as `is_on_foot()` says, or only
  runs?

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
