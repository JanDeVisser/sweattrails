# Activity display overhaul: laps beside the map

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

The activity screen wastes space. Under the data panel's fields, left of
the map, there's an empty area. Meanwhile the lap table takes a
full-width row of its own between the map and the graph, showing only
five laps, and squeezes the graph. Moving the laps into the empty area
gives them more rows and the graph more height. A pen icon after the
title shows that the title can be renamed, which today you have to know.

This is the first of three specs from one list of ideas:

- 40, this one: the layout, and the pen icon on the title.
- [41](41-edit-description.md): editing the description, with a pen icon
  after it and a multiline text editor.
- [42](42-emoji.md): emoji in the title and the description.

## Feature

### What there is now

The activity screen is, top to bottom:

1. the data panel, with the sport icon, title, notes, summary and fields,
   and beside it the map (`SizePolicy::Relative`), hidden for an activity
   without a route;
2. the lap table, full width, `LapTableRows` = 5 rows, hidden for an
   activity with one lap;
3. the graph, `PlotHeight` = 150 px plus its bands.

Clicking the title renames the activity, as F2 and Cmd+Shift+R do (the
`st-rename-activity` task).

### Laps under the fields

- **The lap table moves into the left column**, under the data panel's
  fields, beside the map. It fills the column's height down to the graph,
  and scrolls when there are more laps than fit. The map keeps the right
  side at the full height.
- The full-width row the table had is gone, so the graph gets about that
  height: its plot is 250 px instead of 150, still fixed. A bigger window
  gives the map and the laps the extra room, as now; a plot that grew
  with the window would take it from them on a small screen.
- Clicking a lap, its highlighting, and comparing's Δ columns work as
  they do now.
- **The left column is at least as wide as the lap table**, and the map
  gets narrower to make room, as while comparing, when the table has up
  to ten columns: Lap, Time, Distance, Pace or Speed, Avg HR, Avg Power,
  Cadence, Climb, Δ Power, Δ HR. The data panel's fields are narrower
  than the table anyway. The map stays at least a third of the screen's
  width; on a window too small for both, the table is cut off at the
  right. Scrolling sideways was turned down, as `Table` would need it and
  it's awkward with a mouse, and so was dropping columns.
- One lap: no table, as now, and the area under the fields stays empty.
- No map: the left column is the whole width, and so is the table.

### Map padding

- The map gets 10 px of space all round, the data panel's own margin, so
  that it doesn't touch the data panel, the window's edges or the graph.

### The pen icon on the title

- A pen icon after the title, always there, in grey, so that it's found
  without being loud. Hovering over it turns it white, with the pointing
  hand, as over the title.
- Clicking it renames, as clicking the title and F2 do: it submits
  `st-rename-activity`.
- The icon is a new one in `share/icons`, sized to the title's font.

### Version 1 scope

In: the laps under the fields, the taller graph, the map's padding, and
the pen icon on the title.

Elsewhere: editing the description, in spec 41, and emoji, in spec 42.

## Open questions

None.

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
