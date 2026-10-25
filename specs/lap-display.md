# Lap Display

Status: Ready

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

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
