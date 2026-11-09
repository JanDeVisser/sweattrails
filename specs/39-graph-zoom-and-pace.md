# Zooming the graph, and pace for runs

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
  the running fills the plot's height. When all of it is slower, a
  steep climb zoomed to say, the scale runs from its slowest moving
  speed, `RangeMovingSpeed` or faster, and a rest is drawn at the bottom.
- **The legend** says "Pace 3:50–12:00 min/km", fastest first, as the
  line goes from top to bottom; "Pace 3:50–20:00 min/km" when the
  slowest is the limit. Its ends are rounded to the second, as all paces
  are, and a range whose ends read the same is one pace, "Pace 5:00
  min/km".
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

### What's there

`ActivityGraph` (`src/App/widget/Activity.cpp`) maps x to records by
dividing the plot's width by the number of records in seven places:

- the lines, `render_lines()` and `render_compared()`;
- the ticks, `time_ticks()` and `distance_ticks()` in `GraphData`;
- the selected range;
- the mark;
- `record_at` for hovering and dragging;
- the strip, `lap_spans()` and `lap_at()` in `DisplayedActivity`.

### Decisions

1. **A `GraphView` in `src/App/widget/GraphView.h`**, a header of its
   own so that `DisplayedActivity` doesn't need `GraphData`'s
   dependencies, the records on show:
   `{ size_t first; size_t end; }`, the whole activity or the zoom. With
   it go `view_x(view, record, width)` and `view_record(view, x, width)`,
   so that the seven places map through one pair of functions instead
   of each dividing by the record count. `lap_spans()` gets the view
   too, and clips laps at its edges.
2. **The zoom lives in `DisplayedActivity`**, as
   `std::optional<RecordRange> zoom`, next to the selection it's made
   from. It has `zoom_in()`, `zoom_out()`, `can_zoom_in()` and
   `view()`. A new activity or recording makes a new
   `DisplayedActivity`, so it zooms out without code of its own.
   `zoom_in()` needs a selection of at least 2 records.
3. **The graph renders again when the view changes**: `draw()` compares
   the view with the one it rendered, as `resize()` compares the size.
   Zooming happens in tasks, which don't know about the graph.
4. **The lines over the view**: the series is worked out over the whole
   activity, so the smoothing at the view's left edge has the seconds
   before it, and then sliced to the view for `graph_columns()` and
   `graph_scale()`.
5. **Ticks take the view**: `time_ticks(records, view, width)` and
   `distance_ticks(records, view, width)` count from the activity's
   first record, and pick the step for the view's length.
   - The time steps start at 10 and 30 seconds.
   - Below a minute, a label has seconds, "1:12:30"; otherwise it stays
     "1:12".
6. **Pace is a `GraphValue::Pace`**, the speed series under another
   name, used in place of `Speed` when `is_on_foot()`. Its scale's bottom
   is the slowest speed, but no slower than `SlowestGraphPace`, 20:00
   min/km.
   - `graph_y()` clamps values below the scale to the plot's bottom.
   - If all of it is slower than the limit, a walk up a mountain say,
     the scale is the plain speed range, so that there's still a line.
   - `legend_range()` gives "Pace 3:50–12:00 min/km".
   - `legend_values()` gets the activity, to say "Pace 5:02 min/km", or
     "Pace –" standing still.
7. **Three tasks**, all `.when(on_activity_screen)`:
   - `st-graph-zoom` on `Z`, which toggles, titled "Graph: zoom in or
     out";
   - `st-graph-zoom-in`, "Graph: zoom to the selection";
   - `st-graph-zoom-out`, "Graph: zoom out".

   The menu gets the last two, greyed out with `can_zoom_in()` and when
   not zoomed. `Z` can only toggle, so zooming further in while zoomed
   goes through the menu or the palette.

Nothing new goes in `src/Lib/Widget`.

## Test plan

- `graph_data_test`:
  - `view_x()` and `view_record()`, whole and zoomed;
  - ticks over a view: labels from the activity's start, the steps for
    a short view, seconds in the labels below a minute;
  - the lines sliced to a view, with the smoothing at its left edge
    using the seconds before it;
  - the pace scale: the limit, clamping, and a slow activity;
  - the pace legend and hover values, "Pace –" included.
- `displayed_activity_test`:
  - zooming in and out, and nested;
  - no zoom without a selection or for one record;
  - the selection changing while zoomed;
  - `lap_spans()` over a view, laps cut off at its edges, and `lap_at()`
    on them.
- By hand in the app: drawing zoomed, `Z`, the menu items, dragging and
  the strip while zoomed, and pace on a run.

## Review notes

Reviewed `feature/39-graph-zoom-and-pace` against this spec. Every place
the graph maps x to records (lines, ticks, selected range, mark,
hovering, dragging, strip and its clicks) goes through the view; the
graph renders again when the view changes; the zoom is apart from the
selection, and a new activity or recording zooms out. The full test run
fails only on the new test for finding 1.

1. **Bug.** A slow part with a rest gets a single pace in the legend.
   `legend_range()` (`src/App/widget/GraphData.cpp:270`) gives only the
   fastest pace when `scale.bottom <= 0`. That's the case when all of
   the view is slower than 20:00 min/km and includes a standstill, so
   `graph_scale()` keeps the plain speed range from 0
   (`GraphData.cpp:198`). Zooming into a steep climb on a hike with a
   rest makes it likely: the line goes from the top of the plot to the
   bottom, and the legend says "Pace 23:49 min/km" as if the pace had
   been steady. Fix: give a range here too. Either let the bottom of the
   all-slow scale be the slowest moving speed (at least
   `RangeMovingSpeed`) and clamp the rest to it, or write an open range
   such as "Pace 23:49– min/km". Test:
   `test_pace_legend_of_a_slow_climb_with_a_rest` in `graph_data_test`
   fails.
2. **Missing test.** Slicing the lines to the view isn't tested.
   `ActivityGraph::in_view()` (`src/App/widget/Activity.cpp:548`) is a
   private member, and so is the re-render check (`Activity.cpp:415`).
   `test_series_smoothed_before_the_view` only checks `graph_series()`
   over the whole activity, which hasn't changed. Nothing checks that the
   series is worked out before it's sliced, that the scale is the view's
   (the spec's "the legend gives that part's ranges"), or that both
   series are sliced while comparing. Fix: move it to `GraphData` as a
   free function, `view_series(series, view)`, and test it there: the
   value at the view's first record smoothed with the seconds before it,
   a scale over the view only, and a view past the series' end.
3. **Suggestion.** `Z` during a drag, while zoomed in, zooms out halfway
   through the drag. `cmd_graph_zoom()` (`src/App/SweatTrails.cpp:1075`)
   and `DisplayedActivity::zoom_out()` (`DisplayedActivity.cpp:171`)
   don't check `anchor`, unlike `can_zoom_in()`. The next frame maps
   the mouse to records over the whole activity. The drag carries on
   from its anchor in the zoomed part, so the range's end jumps far
   away. Fix: make `Z` do nothing while dragging, as zooming in already
   does.
4. **Suggestion.** The legend rounds pace one way, and the hover text
   and data panel another. `pace_text()` (`GraphData.cpp:45`) rounds
   with `lround`, while `format_pace()` truncates through `Duration`. At
   3.316 m/s (301.6 s/km) the legend says "5:02" where the hover text
   and the data panel say "5:01". Also, unlike the other lines' legends,
   pace doesn't merge a range whose ends look the same, so a steady
   treadmill run can show "Pace 5:00–5:00 min/km". Fix: one rounding for
   both, and merge equal ends as `label()` does.
5. **Suggestion.** "Zoom to selection" stays enabled when the selection
   is the zoom already, as after `Z` on a lap, and then does nothing.
   The extensions grey out when they wouldn't change anything
   (`SweatTrails.cpp:1979`, `can_zoom_in()` at
   `DisplayedActivity.cpp:159`). Fix: have `can_zoom_in()` also need the
   selection to differ from `zoom`.
6. **Suggestion.** Distance labels have no steps under 1 km
   (`GraphData.cpp:243`). Zoomed to a few minutes of a run, the km
   labels drop to one or none, while the time labels get 10 and 30
   seconds. The spec only promises the time steps, so this is for a
   later spec: 100, 200 and 500 m, labelled "23.5 km".
7. **Suggestion.** The pace scale falls off a cliff on a slow hike. It
   only uses the plain speed range when all of the view is slower than
   3 km/h. A hike at 2.5 km/h with five seconds above 3 km/h gets a
   scale from 3 km/h up to just above it, and nearly all of its line
   lies flat on the bottom. That's what the spec asks for, but it's
   worth checking by hand on a real hike.

Tests added, not committed: in `graph_data_test`,
`test_time_ticks_zoomed_to_the_specs_example`,
`test_time_ticks_with_seconds_over_the_hour`, `test_ticks_of_odd_views`,
`test_pace_legend_at_the_limit`,
`test_pace_legend_of_a_slow_climb_with_a_rest` (fails, finding 1),
`test_pace_hover_on_foot_only` and `test_y_is_clamped`. In
`displayed_activity_test`, `test_lap_spans_zoomed_inside_a_lap`,
`test_zoom_to_a_selection_outside_the_zoom`,
`test_no_zoom_while_dragging_zoomed` and
`test_zoom_after_the_laps_change`. All pass apart from the one for
finding 1.

### Resolution

1. Fixed. All slower than the limit, the pace scale runs from the
   slowest moving speed, and a rest is drawn at the bottom, so the
   legend gives a range. `test_pace_legend_at_the_limit` had a hike
   ending at 40:00 min/km, which is standing still by that rule; it now
   ends at 30:00.
2. Fixed. Slicing is `view_series(series, view)` in `GraphData`, which
   `ActivityGraph` uses for its lines and for both series while
   comparing. Tested: the smoothing before the view's edge, and views
   past the series' end or empty. The scale over the view and the
   comparison's slicing aren't tested apart from that, as they're in
   the drawing.
3. Fixed. `zoom_out()` does nothing during a drag, and `Z` is
   `toggle_zoom()` in `DisplayedActivity`; "Zoom out" in the menu is
   greyed out then too (`can_zoom_out()`).
4. Fixed, by rounding: truncating made an even 30:00, worked out from
   a speed, read 29:59. `pace_minutes()` in `DataFields` rounds to the
   second, and `format_pace()`, the legend and the hover text use it,
   so the data panel's paces are rounded now too. Equal ends merge.
5. Fixed. `can_zoom_in()` is false when the selection is the zoom.
6. Deferred to a later spec: distance steps of 100, 200 and 500 m.
7. Left as the spec asks, to check by hand on a real hike.
