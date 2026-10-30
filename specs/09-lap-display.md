# Lap Display

Status: Done

## Goal

An activity's laps show up in the activity display: interval sessions,
races split by km or mile, rides with a lap at each climb. The user sees
each lap's numbers side by side, and where each lap is on the map and in
the graph, so that intervals can be compared and found.

## How it works now

- **Laps are loaded and analyzed, but not shown.** `Activity::laps` holds
  a `Lap` per FIT lap message (`storage/Activity.h`), with its records
  (`first_record`, `end_record`) and a `Segment` that `analyze()` fills
  the way it fills the activity's own: moving and elapsed time, distance,
  speed, HR, power, NP, IF, VI, TSS, cadence (with the pedaling rule), and
  the climb, which laps work out themselves, since the device's total
  ascent is only for the session. Laps are analyzed before the session,
  from the recorded values (05-activity-display-updates.md). A lap's
  `min_at` and `max_at` are relative to its first record.
- `Lap::title` and `Lap::notes` exist, but nothing fills them: FIT lap
  messages don't have names.
- Many activities have one lap, the whole activity, when the device
  doesn't lap by itself.
- **The display.** `ActivityDisplay` (`widget/Activity.cpp`) is a vertical
  layout: a row with the data panel (`ActivityDataDisplay`, stretching)
  and the map (`ActivityMap`, a third of the width, hidden without GPS),
  and below it the graph (`ActivityGraph`, 200 pixels high).
- **The data panel** shows the activity's fields from `data_fields()`
  (`widget/DataFields.h`), which takes an `Activity`, and picks the fields
  by sport and by the data the activity has (04-activity-data-fields.md).
  Clicking a maximum marks its record.
- **Marks and ranges.** `DisplayedActivity` has a `mark`, a record, and a
  `segment`, a range of records. The graph sets the mark when hovered,
  and the range when dragged; the map draws the range's track in another
  color, and the mark as a dot. The graph draws the mark, but not the
  range.
- **Widgets.** `src/Lib/Widget` has `ListBox` (`Modal.h`), but only as a
  modal (the month's list, query boxes). There's no table, nor a list
  that sits in a layout. `ListBox` scrolls with the keyboard only: the
  arrows, Page Up and Page Down move the selection, and
  `scroll_to_selection()` keeps it in view, with `top_line` and `lines`.
  It has no scrollbar, and doesn't take the mouse wheel.

## Feature

### Where the laps are

- **A lap table** spans the window between the top row (the data panel
  and the map) and the graph. It's a few rows high, and scrolls when
  there are more laps than rows. It's always in view next to the map and
  graph, so a lap selected in it shows there right away. The data panel
  and the map give up the height it takes.
- **A lap strip**, half a character high, runs under the graph, divided
  into the activity's laps, like a ruler. Each lap's part is as wide as
  the lap's part of the graph above it: the graph's x-axis is the record,
  not the time, so the strip divides it by records too, and the laps line
  up with the graph. Neighboring laps alternate between two shades, so
  that each boundary shows. Clicking a lap in the strip selects it, as in
  the table. A very short lap, such as 10 seconds of rest in a two-hour
  ride, is a sliver too narrow to click: the table has it.
- **An activity with one lap** has neither: its one lap is the whole
  activity, and the table would repeat the data panel. The data panel and
  the map keep their height.

### The table

- Five rows high, plus a header. When there are more laps, such as 42 for
  a marathon with a lap per km, it has a scrollbar, and the mouse wheel
  scrolls it. The selected row is kept in view when it's selected with
  the keys.
- Columns: **Lap · Time · Distance · Pace** or **Speed · Avg HR · Avg
  Power · Cadence · Climb**.
  - Lap is the lap's number, from 1.
  - Time is the moving time.
  - Pace or Speed follows the sport, with the units and formats of the
    data panel: speed in km/h for rides, pace in min/km for other sports.
  - Cadence is the average while pedaling, in the sport's unit (rpm,
    spm, /min), as in the data panel.
  - A column shows only if the activity has that data: no Avg Power for a
    run without a power meter, no Climb without altitude. A lap without
    the data, in a column that shows, has an empty cell.
- The table stays narrow on purpose. NP, IF, the maxima and the rest show
  in the data panel when the lap is selected.

### Selecting a lap

- Selecting a lap, in the table or the strip, highlights it:
  - in the table, its row;
  - in the strip, its part;
  - on the map, its part of the track, as a range dragged in the graph is
    drawn now;
  - in the graph, its part.
- **The data panel shows the lap.** Under the title, "Lap 3 of 8" says
  so, and the fields are the lap's, chosen by the same rules as the
  activity's: by sport, and by the data the lap has. Clicking a maximum
  marks its record, as for the activity.
- Esc, or selecting the selected lap again, goes back to the whole
  activity: no lap highlighted, and the activity's fields.
- Dragging a range in the graph while a lap is selected ends the lap's
  selection: the range takes its place, and the data panel goes back to
  the whole activity.

### Keys

Each is a task, for the command palette to come:

| Task                | Key  | Does                                                       |
|---------------------|------|------------------------------------------------------------|
| `st-next-lap`       | Down | Selects the next lap; with none selected, the first        |
| `st-previous-lap`   | Up   | Selects the previous lap; with none selected, the last     |
| `st-whole-activity` | Esc  | Goes back to the whole activity                            |

They do nothing under a modal, such as the month's list. Down and Up do
nothing for an activity with one lap; Esc there still clears a range
dragged in the graph, as it does in an activity with laps. Down on the
last lap, and Up on the first, stay where they are.

### The Table widget

Jan's request for the Developer: the lap table is a **`Table` widget in
`src/Lib/Widget`**, a `Widget` subclass that sits in a layout, not a
modal, and that knows nothing about laps:

- Columns with a header each, rows of cells, and a selected row, if any.
- It reports a click on a row to its owner, which decides what selecting
  means, here selecting the lap or going back to the whole activity.
- **Scrolling is shared with `ListBox`.** The scrollbar and the mouse
  wheel are one piece of code that both widgets use, along with what
  `ListBox` has now for keeping the selection in view (`top_line`,
  `lines`, `scroll_to_selection()`). So `ListBox`, and with it the
  month's list, gets the scrollbar and the mouse wheel too.
- The scrollbar shows only when there are more rows than fit. Its thumb
  shows which part is in view, and how much of the whole that is. The
  thumb can be dragged, and a click on the track above or below it
  scrolls a page. The wheel scrolls a row at a time. Scrolling with the
  bar or the wheel doesn't change the selection.

Whether the lap strip is a widget in `src/Lib/Widget` too, as a bar
divided into parts, the Developer decides.

### Version 1 scope

Everything above. Deferred: a dot on the map where each lap starts: with
a lap per km, a marathon would have 42, and the selected lap's
highlighted track already shows where it is *(deferred)*. Lap names and
notes, which FIT laps don't have *(deferred)*.

## Open questions

None.

## Architecture

### Scrolling, shared by `Table` and `ListBox`: `src/Lib/Widget/Scroll.h`

- `ScrollRange { count, lines, top_line }`: which rows are in view.
  `scroll_to(row)` (what `ListBox::scroll_to_selection()` does now),
  `scroll_by(rows)`, `page(direction)`, and `clamp()`, which keeps
  `top_line` in `[0, count - lines]`. Plain arithmetic, no raylib.
- `Scrollbar`: the bar's geometry and its mouse handling, for a track
  rectangle and a `ScrollRange`:
  - `thumb(track, range)`: the thumb's rectangle, its height the share of
    rows in view, with a minimum so it stays grabbable.
  - `handle(range, track, content, input)`: takes a `ScrollInput { mouse,
    pressed, down, wheel }`, so tests can feed it what raylib would
    report. The wheel over `content` scrolls a row per notch; a press on
    the thumb starts a drag, which maps the mouse's y back to a
    `top_line`; a press on the track above or below the thumb pages.
    Returns whether it took the input, so that a click on the bar doesn't
    also select a row.
  - `draw(track, range)`: the track and the thumb, in screen
    coordinates, only when `count > lines`.
- `ListBox` keeps a `ScrollRange scroll` in place of `lines` and
  `top_line`, and its keys use it. It gets a `process_input()` that
  feeds the scrollbar and the wheel. `ListBoxTest` and
  `ActivitySelectorTest` move from `lines` and `top_line` to
  `scroll.lines` and `scroll.top_line`. `process_input()` calls
  `handle_scrolling(input)`, which tests call with made-up input.

### The `Table` widget: `src/Lib/Widget/Table.h`

- `struct Table : public Widget`, not a modal. `columns` (a header and
  whether it's right-aligned: numbers are), `rows` (a cell per column,
  as strings), `std::optional<size_t> selection`, a `ScrollRange` and a
  `Scrollbar`, and `std::function<void(size_t)> on_click`.
- `set_rows(columns, rows)` resets the selection and the scroll;
  `select(row)` sets the selection and scrolls it into view.
- `row_at(y)` maps a y in the widget to a row, from `row_height`, which
  `resize()` sets from the font: tests set it by hand, as `ListBox`'s
  tests set `lines`.
- Column widths are the widest of the header and the cells, measured in
  `draw()` when the rows are new, since tests have no fonts to measure
  with; there's no wrapping. `rows_area()` and `track()` work out where
  the rows and the scrollbar are from the viewport and `row_height`, and
  `handle_input(input)` takes made-up input, as `Scrollbar::handle()`
  does.
- Its height is `SizePolicy::Characters`, the header and five rows. It
  doesn't decide what a click means: `on_click(row)` does.

### The laps in the app

- **`DisplayedActivity`** moves from `widget/Activity.h` to
  `widget/DisplayedActivity.h`, so that tests can reach it without the
  widgets and raylib, and gets `std::optional<size_t> lap`.
  `select_lap(ix)` sets it and sets `segment` to the lap's records, so
  the map draws it as it draws a dragged range now. `clear_segment()`,
  which a drag in the graph starts with, clears the lap too: that's how a
  drag ends the lap's selection. `next_lap()`, `previous_lap()` and
  `whole_activity()` do what the keys do, `toggle_lap(ix)` what a click
  does, and `has_laps()` says whether there's more than one.
- **`widget/DataFields.h`**:
  - `data_fields(activity, segment, first_record)` picks the fields of
    any segment, the activity's or a lap's, by the activity's sport. It
    adds `first_record` to `mark_at`, since a lap's `max_at` is relative
    to the lap. `data_fields(activity)` stays, for the activity.
  - `lap_table(activity)` returns the table's columns and rows: the
    columns the activity has data for, the cells formatted as the panel
    formats them. This is where the table's logic is, for the tests.
  - `lap_summary(activity, lap)` is the summary of the lap's segment,
    prefixed with "Lap 3 of 8": the panel shows it in place of the
    activity's summary line, under the title.
- **`ActivityDisplay`** gets a `Table` between the top row and the
  graph, hidden (`SizePolicy::Hide`) for one lap. Its `on_click` selects
  the lap, or goes back to the whole activity for the selected one. In
  `on_draw()` the table's selection follows the activity's lap, however
  it was selected, and only scrolls when the lap changed, so that the
  wheel can scroll away from it.
- **The strip** is drawn by `ActivityGraph`, under its graph, so that it
  shares the graph's x scale and lines up with it whatever the width.
  `lap_spans(activity, width)` (in `widget/DisplayedActivity.h`) gives
  each lap's x range, and `lap_at(spans, x)` the
  lap under the mouse: both testable. The graph also shades the selected
  range, a lap's or a dragged one: it doesn't draw ranges now. With one
  lap, there's no strip, and the graph keeps the height.
- **Keys**: `st-next-lap` (Down), `st-previous-lap` (Up) and
  `st-whole-activity` (Esc) in SweatTrails.cpp, calling the
  `DisplayedActivity` functions. The table and the panel follow, as
  above.
- **Keys under a modal.** Bindings run after the modal has handled the
  key, so Esc that closes the month's list would find the modal gone,
  and also go back to the whole activity. `Main::process_input()` notes
  whether a modal was open before the frame's input, and passes it to
  `AppBus::handle_keys()`, which skips the bindings if one was, except
  those of tasks marked with `run_under_modals()`: force quit
  (Ctrl+Shift+Q) is, so that it still gets past a modal. The tasks still
  check for a modal, as they do now, for when they're run otherwise.

## Test plan

- `ScrollTest` (new): `ScrollRange` keeps `top_line` in range for any
  count and lines, `scroll_to()` scrolls just far enough, `page()`;
  `Scrollbar::thumb()` for a few counts, and its minimum height; and
  `handle()` with made-up input: the wheel, a drag of the thumb to the
  bottom and back, a press on the track above and below the thumb, a
  press elsewhere that it doesn't take, and nothing when everything
  fits.
- `ListBoxTest`, `ActivitySelectorTest`: as now, with `scroll`; plus the
  wheel not changing the selection.
- `TableTest` (new), with `ScopedTestApp`: `set_rows()`, `select()`
  scrolling the row into view, `row_at()` for the header, the rows and
  past the last, `on_click` from a click, and a click on the scrollbar
  not reaching `on_click`.
- `DataFieldsTest`: `data_fields()` for a lap (its own fields, `mark_at`
  offset by its first record), `lap_table()` columns by the activity's
  data and sport, an empty cell for a lap without the data, and the
  lap's summary line.
- `DisplayedActivityTest`: `lap_spans()` widths by records and
  adding up to the width, `lap_at()` at the boundaries; and
  `DisplayedActivity`'s lap selection: `select_lap()` sets the range,
  `clear_segment()` clears the lap, `next_lap()` and `previous_lap()` at
  the ends and with none selected.
- `KeyBindingTest`: a binding doesn't run when a modal was open as the
  key was pressed.
- By hand: the table, strip, map and graph with a lapped run and ride,
  the keys, the scrollbar in the table and the month's list, a one-lap
  activity, and the window resized.

## Review notes

1. **Bug: the map is laid out for the wrong height whenever the lap table
   shows or hides.** `ActivityDisplay::set_activity()`
   (`widget/Activity.cpp:643-657`) sets `laps->policy`, then calls
   `map->set_activity()`, and only then `resize()`. `ActivityMap::set_activity()`
   fits the map to `viewport.height` (`Activity.cpp:524`) and places the
   track in screen coordinates, and `ActivityMap::resize()` doesn't redo
   them (the known TODO). So opening a lapped activity after a one-lap one,
   or the first activity at startup if it has laps, builds the map for the
   taller row, after which the row shrinks by the table's height: the
   bottom of the route is cut off by the scissor. The other way round, the
   map is fitted to the shorter row and sits off-center in the taller one.
   Before this feature only the status line caused this; now every switch
   between a lapped and an unlapped activity does. Fix: set the table's
   policy and `resize()` before the children's `set_activity()` (and in
   `clear_activity()` likewise).
2. **Bug: the trackpad doesn't scroll the table or the month's list.**
   `Scrollbar::handle()` (`Scroll.cpp:59`) scrolls by
   `lround(input.wheel)` per frame. A macOS trackpad reports fractions of
   a notch each frame (0.1, 0.25, ...), which round to 0, so the rows never
   move, while `handle()` still returns true and swallows the input.
   `test_trackpad_wheel` in `ScrollTest` fails on this. Fix: keep a
   `float wheel_accumulated` in `Scrollbar`, add `input.wheel` to it, scroll
   by its whole part and keep the rest.
3. **Bug: Ctrl+Shift+Q no longer works while a modal is open.**
   `AppBus::handle_keys()` (`Widget.h:402`) now skips every binding when a
   modal was open. `cmd_force_quit` deliberately has no modal check, unlike
   `cmd_quit`, and was the way out with a query box (or a stuck modal) up;
   on `main` it ran under a modal. The architecture asks for the blanket
   skip, but its reason is only that a key the modal handled (Esc) mustn't
   also run its binding. Fix: skip only the keys the modal's `process_key()`
   took (e.g. record them in `App::process_input()`), or let a binding opt
   in to running under a modal.
4. **Bug: a click in the lap strip that moves up while the button is down
   ends the lap it just selected.** In `ActivityGraph::process_input()`
   (`Activity.cpp:324-326`) a drag starts on any frame where the button is
   down over the graph and `dragging` is false, not only on a press there.
   Press on a lap in the strip (it's selected), move a few pixels up into
   the graph before letting go: the graph sees the button down,
   `clear_segment()` drops the lap, and a range from the mouse starts. The
   same happens when a drag in the graph dips into the strip and comes
   back: the range restarts. Fix: start a drag only on
   `IsMouseButtonPressed` inside the graph area, and while `dragging`,
   keep extending the range (clamped) even over the strip.
5. **Bug (minor, spec): Esc acts on an activity with one lap.** The spec
   says the lap keys "do nothing ... for an activity with one lap", but
   `cmd_whole_activity` calls `DisplayedActivity::whole_activity()`
   (`DisplayedActivity.cpp:94`), which doesn't check `has_laps()`: Esc
   clears a range dragged in the graph of a one-lap activity. Clearing the
   range may well be wanted (it's also what Esc does in a lapped activity
   with no lap selected); then the spec should say so. Otherwise, return
   early without laps, as `next_lap()` does.
6. **Suggestion: `segment_summary()`** (`DataFields.cpp:216`) is outside
   the anonymous namespace but not declared in `DataFields.h`, so it has
   external linkage for no reason. Move it into the anonymous namespace.

Resolution, agreed with Jan:

1. Fixed: `set_activity()` and `clear_activity()` lay out once before
   the children get the activity, so the map is fitted to the row with
   the table's room taken, and once after, for the map hiding without a
   route.
2. Fixed: `Scrollbar` adds up the wheel's fractions of a notch in
   `wheel_total`, and scrolls a row per whole notch.
3. Fixed: a task can opt in to running under a modal with
   `run_under_modals()`; force quit does. `KeyBindingTest` covers it.
4. Fixed: a drag starts only with a press in the graph, and goes on,
   wherever the mouse goes, until the button is let go.
5. Spec changed: Esc clears a dragged range in a one-lap activity too,
   as it does with laps (see Keys).
6. Fixed: `segment_summary()` is in the anonymous namespace.

Tests added by the Reviewer (commit "Add review tests for the lap display
and scrolling"): `ScrollTest::test_trackpad_wheel` (fails, finding 2);
`TableTest`: a thumb dragged over the rows isn't a click, clicks past the
rows, selecting in an empty table; `ListBoxTest`: no scrolling after a
search leaves fewer entries than fit, Page Down after the wheel;
`DisplayedActivityTest`: stepping through and toggling a lap without
records, its zero width in the strip, a lap from the keys replacing a
dragged range; `DataFieldsTest`: a lap's Max Power and elevation marks
offset by its first record, empty Distance and Pace cells for a lap
without distance. All pass, the trackpad test since the fix for
finding 2.
