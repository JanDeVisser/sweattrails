# Activity display overhaul: laps beside the map

Status: In progress

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
- **The table doesn't move as the selection changes.** The data panel
  shows the whole activity's fields, a lap's or a range's, which can
  differ in number: a lap without heart rate has fewer. The panel is as
  high as the most it needs for the activity or any of its laps, while
  comparing too, so the table stays put as you click through the laps. A
  range has the whole activity's fields, so it fits as well. The height
  is worked out again when another activity is shown, its laps change,
  comparing starts or stops, or the window is resized.
- **The map stays a third of the width**, as now, which leaves the left
  column two thirds: room for the table's up to ten columns while
  comparing (Lap, Time, Distance, Pace or Speed, Avg HR, Avg Power,
  Cadence, Climb, Δ Power, Δ HR) on any but a small window. On one too
  small for it, the table is cut off at the right. Scrolling sideways was
  turned down, as `Table` would need it and it's awkward with a mouse, and
  so was dropping columns.
- One lap: no table, as now, and the area under the fields stays empty.
- No map: the left column is the whole width, and so is the table.

### Map padding

- The map gets 20 px of space all round, so that it doesn't crowd the
  data panel, the window's edges or the graph. It has 10 px now, which
  isn't enough.

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

### What's there

- `ActivityDisplay` is a vertical `Layout`: `central`, a horizontal
  `Layout` of the `ActivityDataDisplay` (Stretch) and the `ActivityMap`
  (Relative, 0.33, padding 10); the `OverlayPreview` in `central`'s place
  when it's on; the lap `Table` (Characters, `LapTableRows` + 1); and the
  `ActivityGraph`, whose height `on_resize()` sets from `needed_height()`.
- `Layout` gives each widget its policy's size, with its `padding` inside
  it, and stretches the Stretch ones over the rest. A layout's
  `on_resize()` runs once its own viewport is set, before its widgets'.
- `ActivityDataDisplay::draw()` works out where the title, notes, summary
  and fields go as it draws them.
- `data_fields(activity, segment, first_record, comparison)` gives the
  panel's lines; `DisplayedActivity::comparison()` and
  `lap_comparison(ix)` the comparisons.
- `Table` sizes its scrolling to its height in `resize()`, and doesn't
  clip.

### Decisions

1. **A left column.** `central` becomes the left column and the map. The
   left column is a new `ActivityColumn`, a vertical `Layout` in
   `widget/Activity.h`, Stretch, with the data panel (Absolute) and the
   lap table (Stretch, or Hide for one lap). Its `on_resize()` sets the
   data panel's height, as its width, which the notes wrap to, is known
   by then. `LapTableRows` goes.
2. **The panel's height.** `ActivityDataDisplay::needed_height(width)`
   adds up the same rows `draw()` uses: the sport and date line, the
   title, the notes wrapped to `width`, the summary, and the most field
   lines. The rows' positions move out of `draw()` into one private
   `rows(width)`, so that the two can't drift apart. Measuring needs
   fonts, so this part is checked by hand.
3. **The most field lines** is `most_data_lines(activity, whole,
   laps_compared)` in `DataFields`, next to `data_fields()`, which tests
   reach: the most lines `data_fields()` gives for the whole activity or
   any lap, each with its comparison while comparing, which it takes as
   `lap_table()` does. In `DisplayedActivity` it would have made that
   depend on `DataFields`.
4. **The table clips** its drawing to its viewport, in `Table::draw()`
   in the widget library, so that a table wider than its column is cut
   off there rather than drawn under the map.
5. **The graph's plot** is 250 px: `ActivityGraph::PlotHeight`.
6. **The map's padding** is 20 px.
7. **The pen icon** is `share/icons/pen.svg`, white on transparent,
   turned into `pen-48.png` with `svg2png.swift`, as the app icon is, and
   committed. The data panel loads it with the sport icon, and draws it
   after the title scaled to the title's height, tinted `GRAY`, or
   `RAYWHITE` with the pointing hand while the mouse is over it or the
   title. A click on it does what a click on the title does.

## Test plan

- `data_fields_test`:
  - `most_data_lines()` of an activity with more lines than its laps is
    the activity's.
  - A lap with a field the activity lacks makes it more; one with fewer
    fields doesn't make it less. Without laps, the activity's.
  - While comparing, the comparisons' lines count.
- By hand, after `ninja -C build install`:
  - a run with laps: the table under the fields, beside the map, filling
    down to the graph, and scrolling with many laps;
  - clicking through the laps and dragging a range: the table doesn't
    move;
  - one lap: no table; no map, as on a treadmill: the table full width;
  - comparing: the Δ columns, and on a small window the table cut off at
    the map;
  - the graph taller, the map's padding;
  - the pen: grey, white on hover over it or the title, a click renames.

## Review notes

*Filled in by the Reviewer.*
