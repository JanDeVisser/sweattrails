# Stats for a range dragged in the graph

Status: Done

## Goal

The user drags across part of an activity in the graph, such as a climb
or an interval that wasn't a lap of its own, and the data panel shows
that part's stats: its distance, time, speed or pace, heart rate, power,
cadence and elevation, just as it does for a lap. Now a dragged range is
only drawn on the map and in the graph, and the panel stays on the whole
activity (`specs/09-lap-display.md`).

## Feature

### What there is now

- Dragging in the graph selects a range of records
  (`DisplayedActivity::segment`). The map draws its part of the track in
  blue, and the graph shades it. A drag ends a lap's selection, and the
  data panel goes back to the whole activity.
- A lap selected in the table, the strip under the graph, or with
  Up/Down shows its own fields in the data panel, under "Lap 3 of 8 ·
  <summary>" (`lap_summary()`, `data_fields(activity, segment,
  first_record)`). Esc goes back to the whole activity.
- A lap's distance, elapsed time and moving time come from the FIT
  file's lap message; `Segment::analyze()` works out the rest (ranges of
  speed, power, heart rate, cadence, elevation, climb, NP and so on) from
  the records. A dragged range has no lap message, so its distance and
  times have to come from its records.
- Dragging leftwards, from where the drag started, doesn't select
  anything useful: `set_segment()` starts the range afresh at each record
  left of its start, so the range ends up one record wide.

### Showing a range's stats

- **When the drag ends**, when the button is let go, the data panel
  shows the range's stats. While dragging, it stays as it was: working
  the stats out on every frame isn't needed to see where the range is,
  and the map and graph already show that.
- **The same fields as a lap**, by the same rules: by sport, and by the
  data the range's records have. Clicking a maximum marks its record, as
  for a lap.
- The range's distance and elapsed time come from its records: the
  distance from the first record's to the last's, the elapsed time from
  their timestamps. Moving time leaves out the stops in between.
- Esc goes back to the whole activity, as it does for a lap: the range
  is cleared, and the panel shows the activity's fields.

### The heading

- Under the title, where a lap says "Lap 3 of 8 · <summary>", a range
  says where it is, by distance from the start: "12.4–18.9 km ·
  <summary>", with the summary as a lap's (its distance, moving time,
  and TSS where there is one). That tells two ranges of the same ride
  apart, which "Selection" wouldn't.
- An activity whose records have no distance, such as one indoors
  without a speed sensor, says where it is by time from the start
  instead: "0:42:10–1:05:33 · <summary>".

### Dragging either way

- A drag selects from where it started to where the mouse is, to the
  left as well as to the right, and it can cross back over its start.
  Now dragging leftwards keeps starting the range afresh, and leaves it
  one record wide: half of all drags would show useless stats.

### Short ranges

- A range shorter than 10 seconds, from its first record's time to its
  last's, isn't a selection: when the button is let go, it's cleared,
  and the panel stays on the whole activity. A plain click, which makes
  a range of one record, is one of these, so a click still only marks
  the record, as now. Stats of a few seconds mean little, and a stray
  blue dot on the map would only puzzle.

### Version 1 scope

- The stats of a dragged range in the data panel, with its heading.
- Dragging leftwards selects a range too.
- Ranges under 10 seconds are cleared when the drag ends.
- Zooming the map to the range stays *(deferred)*, as in
  `specs/10-map-pan-zoom.md`.

## Open questions

None.

## Architecture

### The range's segment (`storage/Activity.*`)

- **`Activity::recorded`**: a copy of the records as the FIT file has
  them, made by `analyze()` before it smooths `records` for the graph.
  The laps are analysed on the records as recorded; a range analysed on
  the smoothed ones would be smoothed twice, and a range over exactly a
  lap would show other maxima, NP and cadence than the lap. About 100
  bytes a record: 2 MB for a five-hour ride.
- **`Segment range_segment(Activity const &, size_t first, size_t last)`**,
  pure: the segment of `recorded[first..last]`, as a lap's would be:
  - `start_time` and `end_time` from the first and last records,
    `elapsed` between them;
  - `distance` the last record's `distance` minus the first's, when both
    have one;
  - `moving` the sum of the gaps between consecutive records that are
    `RangeMovingGap` (10 s) or less, and, where the record has a speed,
    whose speed is at least `RangeMovingSpeed` (0.5 m/s). A longer gap is
    a pause (auto-pause records nothing); a record standing still is a
    stop. This is close to, not the same as, the device's timer.
  - The rest from `Segment::analyze(…, false)`, after `moving` is set, as
    TSS is over the moving time.

### The selection (`widget/DisplayedActivity.*`)

- **Dragging either way**: `start_segment(t)` starts a drag at `t`, its
  anchor, and `set_segment(t)` makes the range run from the anchor to
  `t`, whichever side it's on. `ActivityGraph` calls `start_segment()`
  on the press, where it calls `clear_segment()` and `set_segment()`
  now.
- **`end_segment()`**, which `ActivityGraph` calls when the button is let
  go: a range shorter than `MinRangeSeconds` (10 s) is cleared; a longer
  one gets `range_stats`, its `range_segment()`.
- **`std::optional<Segment> range_stats`**: the dragged range's stats,
  for the data panel. `clear_segment()` (a new drag, Esc) and
  `select_lap()` clear it.

### The data panel (`widget/DataFields.*`, `widget/Activity.cpp`)

- **`range_summary(activity, first, last, segment)`**, pure:
  "12.4–18.9 km · <summary>", by the records' distance from the
  activity's start, or, without distance, "0:42:10–1:05:33 · <summary>",
  by their time from the first record. The summary is a lap's
  (`segment_summary()`).
- `ActivityDataDisplay::draw()` and `draw_fields()` take the range's
  summary and `data_fields(activity, *range_stats, segment->min)` when
  there are `range_stats`, as they take a lap's now.

Nothing here belongs in `src/Lib/Widget`: it's all the app's data.

## Test plan

- `ActivityLoadTest` (FIT files built with `FITBuilder`):
  - `recorded` keeps the records as recorded while `records` is
    smoothed;
  - `range_segment()`: distance, elapsed, start and end; moving time
    leaves out a gap of more than 10 s and records standing still;
    a range over exactly a lap has the lap's maxima and averages; a
    range without distance.
- `DisplayedActivityTest`:
  - dragging right, left, and back across the anchor;
  - `end_segment()`: under 10 s clears the range, a plain click (one
    record) too; 10 s or more sets `range_stats`;
  - a new drag, Esc and selecting a lap clear `range_stats`.
- `DataFieldsTest`: `range_summary()` by distance and by time; the
  fields' marks are records of the activity, not of the range.
- By hand: dragging in the graph, both ways, and the panel after it;
  Esc; a click still marking a record.

## Review notes

1. **Bug: a lap selected mid-drag leaves the lap with another range.**
   `DisplayedActivity::set_segment()` (`DisplayedActivity.cpp:51`)
   re-anchors when there's no anchor, and `ActivityGraph::process_input()`
   (`Activity.cpp:363`) keeps calling it until the button is let go.
   Pressing Down (or Up) while dragging runs `select_lap()`, which resets
   the anchor but leaves `lap` set; the next frame's `set_segment(t)`
   makes `segment` `[t, t]`, growing with the mouse, and `end_segment()`
   returns early because `lap` is set. The panel shows the lap's stats,
   the map and graph a range that isn't the lap. Esc mid-drag and Left/Right
   to another activity mid-drag go the same way: the drag isn't cancelled,
   it restarts where the mouse is, and gets stats on release. Fix: let
   `start_segment()` set the anchor, and have `set_segment()` do nothing
   without one, so that Esc, a lap or a new activity ends the drag; or
   `lap.reset()` in `set_segment()`. Test:
   `test_lap_selected_during_a_drag` (fails).
2. **Bug: a range whose time goes back gets stats of 136 years.**
   `end_segment()` (`DisplayedActivity.cpp:67`) and `range_segment()`
   (`Activity.cpp:166`) subtract `u32` timestamps, first from last. When
   the last record's time is earlier than the first's, as after a watch
   corrects its clock, the difference wraps: the range passes the 10 s
   check and `elapsed` is about 4.29e9 s. Fix: compare and subtract as
   signed (`i64`), treat a negative span as under `MinRangeSeconds`, and
   clamp `elapsed` at 0. Test: `test_range_with_time_going_back` (fails).
3. **Suggestion: the heading of a range standing still.**
   `range_summary()` (`DataFields.cpp:252`) goes by time when the range's
   `segment.distance` is 0, also in an activity that has distance, so a
   10 s+ range at a stop says "0:42:10–0:42:30" where every other range
   of the ride says km. The spec's rule is the activity's records having
   no distance; testing `from.distance && to.distance` alone would follow
   it.
4. **Missing test (added): a range over a lap has the lap's power, NP and
   heart rate.** The Developer's test compared climb, cadence and
   elevation only; the graph's smoothing changes power and heart rate
   most. `test_range_over_a_lap_has_its_power_and_heart_rate` (passes).
5. **Missing test (added): TSS over the moving time.**
   `test_range_tss_is_over_the_moving_time`: a 10-minute pause in a
   range doesn't add to its TSS (passes).
6. **Missing test (added): the time heading past an hour, and out of
   bounds.** `test_range_summary_by_time_past_an_hour`, the spec's own
   "0:42:10–1:05:33" example (passes).

### Resolution

1. Fixed: only `start_segment()`, a press in the graph, starts a drag,
   and `set_segment()` does nothing without one. Esc, selecting a lap
   and another activity end a drag; `test_esc_during_a_drag` was added.
2. Fixed: the times are subtracted signed. A range going back in time is
   too short to select, and `range_segment()`'s elapsed and moving times
   don't go below zero.
3. Fixed: the heading goes by whether the records have distance, so a
   range at a stop says where it is in km.
4.–6. The Reviewer's tests are kept.
