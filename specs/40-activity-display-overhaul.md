# Activity display overhaul: laps beside the map

Status: In review

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

1. **Bug: a table sized by the Characters policy loses a row at some font
   sizes, which breaks the summaries' table.** `Table::resize()` now calls
   `fit_rows()` (src/Lib/Widget/Table.cpp:115-130), which sets
   `scroll.lines` from the height. `Layout` gives a Characters widget
   `ceilf(1.2 * n * char_y)`, worked out in double, while `row_height` is
   `1.2f * char_y` in float, a hair more. When `1.2 * (rows + 1) * char_y`
   is a whole number, the height divided by `row_height` comes out just
   under `rows + 1`, and `floor()` drops a row. With `font_size` 25 in the
   settings (or 45), the summaries' table (`TableRows` = 8) shows 7 rows and
   a scrollbar; on main it showed 8, as `scroll.lines` kept the
   constructor's value. The same arithmetic hits `ActivityColumn::on_resize()`
   (src/App/widget/Activity.cpp:780-791): `least_table` is
   `3 * 1.2f * char_y`, the layout floors what's left to the table, and at
   the default font size 20 the table that's supposed to keep "its header
   and two rows" (Activity.h) gets 72 px for rows of 24.000001 px: a header
   and one row. A tolerance in `fit_rows()` (or one rounding of the row
   height shared by `Layout` and `Table`) fixes both.
   Failing test: `test_characters_table_shows_its_rows` in `table_test`.
2. **Bug: a lap table that grows keeps empty lines under its last row
   while rows above it are out of view.** `fit_rows()` (Table.cpp:121-130)
   sets `scroll.lines` but doesn't clamp `top_line`, and `scroll_to()` only
   moves it far enough to keep the selection in view. Sequence: 20 laps,
   the table shows 5; scroll to the bottom (or select the last lap), so
   `top_line` is 15; stop comparing, or make the window taller, so the table
   now fits 11 rows. It shows laps 16 to 20 and six empty lines, and laps
   10 to 15 are only reachable by scrolling up. This happens in the normal
   flow, as the spec makes the table's height change when comparing starts
   or stops. `fit_rows()` needs a `scroll.clamp()` after setting `lines`.
   Failing test: `test_fit_rows_keeps_the_rows_filled` in `table_test`.
3. **Bug (small windows): the data panel keeps a stale height when it has
   to give way completely.** In `ActivityColumn::on_resize()`
   (Activity.cpp:790) `policy_size` becomes 0 when the column is shorter
   than `least_table` (about 72 px at font size 20, i.e. a window a little
   over 400 px high now that the graph needs 250 px plus its bands). But
   `Layout::resize()` only sets a widget's size when `sz > 0`
   (Layout.cpp:73), so the panel keeps the height of the last resize, and
   the table is placed below that, outside the column and over the graph,
   where it still takes clicks (`Layout::process_input()` gives the column
   input before the graph). Related, and not new but now reached at a
   larger height for an activity with one lap: with no lap table and no
   minimum window size, a window shorter than the graph's
   `needed_height()` hits `assert(total > allocated)` in `Layout::resize()`
   for `ActivityDisplay` in a Debug build. Before, that took a window under
   150 px plus the bands; now it's 250 px plus the bands. A
   `SetWindowMinSize()` would settle both.
4. **Bug (minor): the pen stays white while the rename box is open.**
   `renaming_hovered` is only updated in `ActivityDataDisplay::process_input()`
   (Activity.cpp:224), which doesn't run while a modal is open. Clicking
   the pen opens the input box, and the pen stays highlighted as if hovered
   until the box closes and the mouse is processed again. The same happens
   if the summaries are shown while the mouse is over the title and you
   come back. Resetting it when the click submits the rename, or working
   the colour out in `draw()` from the mouse position, avoids it.
5. **Suggestion: load the pen icon once, as the architecture says.**
   Decision 7 says the panel loads the pen with the sport icon; `draw_pen()`
   (Activity.cpp:125-129) loads it lazily in `draw()` whenever `pen_icon.id`
   is 0. If `pen-48.png` isn't in `build/share/icons`, which is the case
   for a bundle that wasn't reinstalled after this branch (the install step
   is the only thing that copies it), `LoadTexture()` fails and is retried
   every frame, with a raylib file warning in the log each time, and no
   pen. Loading it in `initialize()` or `set_activity()` tries once.
6. **Missing test (added): the pen's files.** Nothing checked that
   `share/icons/pen.svg` and `pen-48.png` are in the source tree, as
   `sport_icon_test` does for the sport and app icons. Added
   `test_pen_icon_files_exist` there; it passes.

Checked and fine: `rows()` puts the title, notes, summary and fields
where `draw()` on main did (the title's y used the measured date line,
which is the font's height either way; the summary's half-line gap after
the notes is kept). `needed_height()` uses the same field line height as
`draw_fields()`, and the panel has no padding, so the column's width is
the width the notes wrap to. The height is worked out again on every path
the spec lists: `show()` (another activity, a recording, a reloaded
activity with new notes), `laps_changed()`, `compare_with()`,
`clear_activity()`, `show_preview()` and the window's resize; a rename
changes only the title, which is one line and doesn't change the height.
The scissor in `Table::draw()` matches how `ActivityMap` and the overlay
preview clip, and is harmless for the summaries' table, whose rows fit
its viewport.

### Resolution

1. Fixed. `fit_rows()` allows for the float error, so a table sized in
   characters shows all its rows, and the lap table its header and two
   rows at least. `test_characters_table_shows_its_rows` passes.
2. Fixed. `fit_rows()` clamps the scroll, so a table that grows shows
   the rows above. `test_fit_rows_keeps_the_rows_filled` passes.
3. Fixed. The window can't be made smaller than 800x600, its size at
   start-up (`SetWindowMinSize()` in `App`), and the data panel's height
   is never 0. Checked by hand, as it needs a window.
4. Fixed. Whether the pen is hovered is worked out as it's drawn, and
   not while a modal is open.
5. Fixed. The pen is loaded once, when the first activity is shown.
6. Kept: `test_pen_icon_files_exist`.
