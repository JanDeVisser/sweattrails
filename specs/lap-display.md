# Lap Display

Status: In progress

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
  from the recorded values (activity-display-updates.md). A lap's
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
  by sport and by the data the activity has (activity-data-fields.md).
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

They do nothing under a modal, such as the month's list, or for an
activity with one lap. Down on the last lap, and Up on the first, stay
where they are.

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
  - `draw(widget, track, range)`: the track and the thumb, only when
    `count > lines`.
- `ListBox` keeps a `ScrollRange scroll` in place of `lines` and
  `top_line`, and its keys use it. It gets a `process_input()` that
  feeds the scrollbar and the wheel. `ListBoxTest` and
  `ActivitySelectorTest` move from `lines` and `top_line` to
  `scroll.lines` and `scroll.top_line`.

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
  `resize()`; there's no wrapping, and the last column takes what's
  left.
- Its height is `SizePolicy::Characters`, the header and five rows. It
  doesn't decide what a click means: `on_click(row)` does.

### The laps in the app

- **`DisplayedActivity`** gets `std::optional<size_t> lap`.
  `select_lap(ix)` sets it and sets `segment` to the lap's records, so
  the map draws it as it draws a dragged range now. `clear_segment()`,
  which a drag in the graph starts with, clears the lap too: that's how a
  drag ends the lap's selection. `next_lap()`, `previous_lap()` and
  `whole_activity()` do what the keys do.
- **`widget/DataFields.h`**:
  - `data_fields(activity, segment, first_record)` picks the fields of
    any segment, the activity's or a lap's, by the activity's sport. It
    adds `first_record` to `mark_at`, since a lap's `max_at` is relative
    to the lap. `data_fields(activity)` stays, for the activity.
  - `lap_table(activity)` returns the table's columns and rows: the
    columns the activity has data for, the cells formatted as the panel
    formats them. This is where the table's logic is, for the tests.
  - `activity_summary()` gets a segment version for the lap, prefixed
    with "Lap 3 of 8": the panel shows it in place of the activity's
    summary line, under the title.
- **`ActivityDisplay`** gets a `Table` between the top row and the
  graph, hidden (`SizePolicy::Hide`) for one lap. Its `on_click` selects
  the lap, or goes back to the whole activity for the selected one.
- **The strip** is drawn by `ActivityGraph`, under its graph, so that it
  shares the graph's x scale and lines up with it whatever the width.
  `lap_spans(activity, width)` (in `widget/DataFields.h` or a small
  `widget/Laps.h`) gives each lap's x range, and `lap_at(spans, x)` the
  lap under the mouse: both testable. The graph also shades the selected
  range, a lap's or a dragged one: it doesn't draw ranges now. With one
  lap, there's no strip, and the graph keeps the height.
- **Keys**: `st-next-lap` (Down), `st-previous-lap` (Up) and
  `st-whole-activity` (Esc) in SweatTrails.cpp, calling the
  `DisplayedActivity` functions, and then updating the table and the
  panel.
- **Keys under a modal.** Bindings run after the modal has handled the
  key, so Esc that closes the month's list would find the modal gone,
  and also go back to the whole activity. `Main::process_input()` notes
  whether a modal was open before the frame's input, and
  `AppBus::handle_keys()` skips the bindings if one was. The tasks still
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
- `LapTest` or in `DataFieldsTest`: `lap_spans()` widths by records and
  adding up to the width, `lap_at()` at the boundaries; and
  `DisplayedActivity`'s lap selection: `select_lap()` sets the range,
  `clear_segment()` clears the lap, `next_lap()` and `previous_lap()` at
  the ends and with none selected.
- A test that a binding doesn't run when a modal was open as the key was
  pressed.
- By hand: the table, strip, map and graph with a lapped run and ride,
  the keys, the scrollbar in the table and the month's list, a one-lap
  activity, and the window resized.

## Review notes

*Filled in by the Reviewer.*
