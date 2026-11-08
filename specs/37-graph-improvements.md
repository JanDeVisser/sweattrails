# Graph improvements

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

The activity screen's graph shows the shape of an activity, but not its
numbers: there are no axes, and nothing says which line is which or what
its scale is. Its power and speed lines are 1-second noise, in dark
colours that are hard to read. This spec makes the graph readable: time
and distance along it, a legend with each line's range, and smoothed
power and speed in lighter colours. It also fixes two flaws: the graph
doesn't follow the window's size, and a dropout draws a straight line
where there's no data.

Extending the selection with a right-click, to the next km, lap or stop,
is spec 38, which builds on this one.

## Feature

### The graph as it is

The graph under the data panel and the map (`ActivityGraph` in
`src/App/widget/Activity.cpp`) is 200 pixels high and as wide as the
window less 20 pixels. It's drawn once into an image when the activity is
set:

- Altitude is a light gray area, over the activity's elevation range.
- Speed is a dark green line from 0 to the highest speed, also for runs.
- Heart rate is a red line over its range, from lowest to highest.
- Power is a dark blue line from 0 to the highest power.
- Cadence isn't drawn, except when comparing recordings (spec 33), which
  draws power, heart rate and cadence of both recordings instead.

The x axis is the record's index, not time or distance, so a pause that
the device recorded as a gap without records takes no room. There are
no axes, ticks or labels, and no legend. Every record is drawn
unsmoothed, so 1-second power and GPS speed are noisy. Outside comparing,
a dropout in heart rate or power isn't a gap: the line jumps from the
last value to the next one. `resize()` does nothing, so the graph keeps
its width when the window changes size until another activity is shown.

Hovering shows a mark with a banner of the record's altitude, power,
speed and heart rate. Dragging selects a range (spec 25). The lap strip
under the graph selects a lap (spec 09).

### Layout

From top to bottom, the graph widget has:

1. **The legend**, one row in the small font, at the top left.
2. **The km labels**, one row in the small font, for activities with a
   distance.
3. **The plot**, as high as now.
4. **The lap strip**, as now.
5. **The time labels**, one row in the small font.

The widget grows by one small-font row, so that the plot keeps its
height. The data panel and the map get that much less room. The top band
that holds the hover banner now has the legend and the km labels in its
place.

The x axis stays by record, as now, so that the lap strip, dragging, the
selected range and the mark all work as they do. The labels follow the
records.

### Time and distance labels

- **Time** runs along the bottom, under the lap strip. It's the
  elapsed time since the activity's first record, as in "0:10" or
  "1:30", in hours and minutes. Each label goes at the first record at
  or past its time, with a short tick mark down from the plot. A pause
  that recorded nothing bunches the labels on either side of it, which
  shows where it was. If it lasted longer than a step, the record after
  it gets the last step it passed: "0:30" after a pause from 0:08 to
  0:35, not "0:10".
- **Distance** runs along the top of the plot, under the legend: "5 km",
  at the first record at or past it, with a short tick mark up from the
  plot. An activity without distances, such as an indoor ride without a
  speed sensor, has no km row, and the band is left empty.
- The steps are chosen so that labels are at least about 80 pixels
  apart, from 1, 2, 5, 10, 15 and 30 minutes and 1 and 2 hours for time,
  and 1, 2, 5, 10, 20, 50 and 100 km for distance. `nice_span()` in
  `Chart.h` already does this for the summaries' charts.
- The labels are in kilometres, as the rest of the app is.

### Legend

The legend lists the lines drawn, each in its line's colour, as
"Alt 12–86 m", "Spd 0–48 km/h", "HR 102–178 bpm" or "Pwr 0–612 W". The
range is the one the line is drawn over: from 0 to the highest value for
power and speed, and from the lowest to the highest for heart rate and
altitude. For power and speed that's the highest smoothed value, so
that the line fills the plot, and it can be below the data panel's
maximum.

While the mouse is over the graph, the legend shows the values at the
mark instead: "Alt 45 m  Spd 32.4 km/h  HR 145 bpm  Pwr 245 W". The
floating banner next to the mark goes away, while the mark's vertical
line stays. A value the record doesn't have is left out, as the banner
does now. The values are the record's own, not smoothed, as the data
panel's are.

A faint horizontal line across the middle of the plot helps to read the
lines against their ranges.

While comparing recordings (spec 33), the legend lists power, heart
rate and cadence, with both recordings' ranges taken together, as the
lines are drawn. While hovering, it shows the values of both, as the
banner does now, from `compare_banner()`: "245 / 238 W".

### Smoothing

- Each pixel column draws the average of the records in it, where it now
  draws one of them and skips the rest.
- Power and speed are smoothed with a 5-second rolling average before
  that. Heart rate, cadence and altitude are drawn as they are.
- The rolling average doesn't reach across a pause of more than 10
  seconds without records, `RangeMovingGap`, nor across a dropout.
- The same smoothing applies while comparing, to both recordings' power.
- There's no setting or key for it.

### Fixes

- **Resizing.** The graph is drawn again when its widget changes size,
  such as when the window is resized or maximized, and not only when
  another activity is shown.
- **Dropouts.** A record without heart rate, power or speed breaks that
  line: it's a gap, as it is while comparing, and doesn't join the
  values on either side with a straight line. Altitude's area is left as
  it is.

### Colours

Dark blue power and dark green speed are hard to read on the dark
background, so they get lighter:

- **Power** becomes raylib's `SKYBLUE`, (102, 191, 255), in place of
  `DARKBLUE`. `PowerColor` in `DataFields.h` changes, so this applies
  wherever power is shown: the graph's line, the legend, the data panel
  while comparing, and the compared lines, where the other recording's
  stays a lighter shade of it.
- **Speed** becomes raylib's `LIME`, (0, 158, 47), in place of
  `DARKGREEN`. It gets a `SpeedColor` constant next to `PowerColor`,
  `HeartRateColor` and `CadenceColor`, used for its line, the legend and
  the hover values, where `DARKGREEN` is now written out.

### Version 1 scope

In: the layout, time and km labels, the legend with ranges and hover
values, smoothing, the lighter power and speed colours, and the two
fixes.

Deferred:

- Extending the selection with a right-click, to the next km, lap or
  stop: spec 38 *(deferred)*.
- Pace instead of speed for runs, turning lines on and off, and cadence
  outside comparing *(deferred)*.
- Zooming into a range *(deferred)*.
- Miles *(deferred)*.

## Open questions

None.

## Architecture

### What's there

- `ActivityGraph` in `src/App/widget/Activity.cpp` draws the lines into
  an `Image` once, in `set_activity()`, and loads it into a texture.
  `draw()` draws the texture, the selected range, the strip, the mark
  and the banner. `draw_compared()` draws the comparison, with a
  `GraphLine` helper that already breaks a line at a missing value.
- `Activity::records` aren't as recorded: `Segment::analyze()` smooths
  speed, power, cadence and heart rate over 3 records in place.
  `Activity::recorded` has them as recorded, with the same indices.
  The graph and the banner use `records` now.
- `nice_span()` and `visible_labels()` in `src/Lib/Widget/Chart.h` pick
  a step for labels and drop those that would overlap.

### Decisions

1. **The logic goes in a new `src/App/widget/GraphData.h/.cpp`.** These
   are pure functions of the records and a width, so the test reaches
   them without a window. `ActivityGraph` stays in `Activity.cpp` and
   only draws what they return.

   ```cpp
   enum class GraphValue { Altitude, Speed, HeartRate, Power, Cadence };

   // A value of each record: the recorded one, or for power and speed the
   // average over the last 5 seconds. None for a record without one; the
   // average restarts after a pause of more than RangeMovingGap seconds
   // and after a dropout.
   std::vector<std::optional<float>> graph_series(std::span<record const> recorded, GraphValue value);

   // The series per pixel column of a graph `width` wide: the average of
   // the records in each column, none for a column without values.
   std::vector<std::optional<float>> graph_columns(std::span<std::optional<float> const> series, int width);

   struct GraphTick {
       float       x;      // From the graph's left edge
       std::string label;  // "1:30", "5 km"
   };
   std::vector<GraphTick> time_ticks(std::span<record const> records, float width);
   std::vector<GraphTick> distance_ticks(std::span<record const> records, float width);

   // The legend: "Pwr 0–612 W", or while hovering "Pwr 245 W".
   std::vector<GraphLabel> legend_ranges(...);
   std::vector<GraphLabel> legend_values(record const &recorded);
   ```

   `compare_banner()` stays in `DataFields.cpp`, and the legend uses it
   while comparing.
2. **Smoothing reads `Activity::recorded`, not `records`.** Otherwise
   the 5-second average would be applied on top of the existing 3-record
   one. The legend's hover values come from `recorded` too, as the spec
   asks. That changes what the old banner showed, the 3-record smoothed
   values, to the record's own.
3. **The ticks' step:** `nice_span(0, total, width / 80, steps)` picks
   the step from the spec's lists. A label goes at the first record at
   or past its time or distance. `visible_labels()` then drops those a
   pause bunches into each other.
4. **Layout:** the widget becomes 220 pixels high instead of 200. The
   top band stays 50 pixels: the legend at the top and the km labels
   under it. The extra 20 pixels at the bottom take the time labels,
   under the strip. The plot keeps its height.
5. **Resizing:** `set_activity()` and `resize()` both call a private
   `render()`. `resize()` only renders again when the plot's width or
   height has changed, since the layout resizes every widget whenever
   any of it changes.
6. **Colours:** in `DataFields.h`, `PowerColor` becomes `SKYBLUE`, and
   `SpeedColor = LIME` and `AltitudeColor = LIGHTGRAY` join it. The
   graph code stops spelling out `DARKGREEN` and `LIGHTGRAY`.

Nothing new goes in `src/Lib/Widget`. The ticks reuse `Chart.h`. The
legend is a row of coloured text, as the banner was, so it doesn't need
a widget of its own.

## Test plan

A new `graph_data_test` (`test/GraphDataTest.cpp`) tests the functions
in `GraphData.h` on records built in the test:

- `graph_series()`:
  - a 5-second average of power and speed;
  - no smoothing for heart rate and altitude;
  - a restart after a gap of more than 10 s;
  - a dropout gives none, and the average after it doesn't include the
    values before it;
  - it reads the recorded values.
- `graph_columns()`:
  - averaging when there are more records than pixels;
  - one record over several columns when there are fewer;
  - none for a column of dropouts.
- `time_ticks()` and `distance_ticks()`:
  - the step for a short and a long activity;
  - labels at the first record at or past each step;
  - bunching around a pause;
  - none without distances.
- `legend_ranges()` and `legend_values()`:
  - the texts, the colours, the smoothed maximum, and values left out
    when the record has none;
  - comparing.

The dropout fix is a fix to `graph_series()` and `graph_columns()`, and
their tests cover it.

Drawing and resizing need a window, so they're checked by hand in the
app: the layout, the legend while hovering and not, the colours, and
that a window resize redraws the graph.

## Review notes

1. **Bug: the legend never goes back to the ranges once the graph has
   been hovered.** `draw_legend()` (`src/App/widget/Activity.cpp:350-361`)
   shows the values whenever `activity->mark` is set, but the mark is only
   cleared by a press in the graph (`Activity.cpp:449`): `process_input()`
   returns at `if (!mouse_in())` (`:445`) without clearing it, and nothing
   else does. So moving the mouse over the graph and then off it leaves
   the hover values in the legend for as long as the activity is shown,
   and the ranges are only visible before the first hover. Clicking a
   maximum in the data panel also puts values in the legend without the
   mouse being over the graph. The spec says the values show "while the
   mouse is over the graph". Fix: in `draw_legend()`, show the values only
   while `mouse_in()` (and the mark is set), else the ranges; keep the
   mark itself for the map's dot and the mark's line. Not tested: it
   needs mouse input.
2. **Bug: when the clock goes back, the 5 s average becomes a running
   average until the clock catches up.** `RollingAverage::add()`
   (`src/App/widget/GraphData.cpp:69-77`) only restarts on a forward gap
   and only drops samples from the front once `front.at + 5 <= at`. After
   a record earlier than the one before it, the front stays later than
   every new `at`, so nothing is dropped and every value since the jump is
   averaged in, along with the 5 s before it: an hour's clock correction
   gives an hour-long running average of power and speed. Fix: also
   `clear()` when `at < window.back().at`. Test:
   `test_average_restarts_when_the_clock_goes_back`, which fails.
3. **Bug (against the spec): the legend's ranges come from the column
   averages, not from the values.** `render_lines()` and
   `render_compared()` (`Activity.cpp:505`, `:542`) call `graph_scale()`
   on `graph_columns()`. With more records than pixels (a 3 h ride is
   10,800 records on a ~1,000 px plot) each column averages ~10 records,
   so "HR 102–178 bpm" shows as something like "HR 105–172 bpm": the
   heart rate and altitude ranges no longer match the data panel's
   min/max, which the spec asks for ("from the lowest to the highest"),
   and power and speed are below "the highest smoothed value". The
   numbers also change when the window is resized. Fix: take the scale
   from the `graph_series()` values (the line then fills the plot nearly,
   not exactly), or change the spec to say the ranges are those of the
   drawn columns.
4. **Suggestion: the bands are fixed in pixels, the labels aren't.**
   `TopBand = 50` and `BottomBand = 20` (`src/App/widget/Activity.h:116-117`),
   and the km labels at `TopBand - 22` (`Activity.cpp:382`), fit the
   default small font (15 px). The small font is 3/4 of the
   `appearance.font_size` setting: from a font size of about 28, the time
   labels (`below + 3`, `:384`) run out of the widget into the one below,
   and the legend runs into the km labels. The spec asks for "one
   small-font row"; derive the bands from `char_size(FontSize::Small).y`,
   as `strip_height()` already does with `char_size()`.
5. **Suggestion: the hover legend is a guess at "over the graph".**
   `mouse_in()` also covers the legend, the km row, the time row and the
   20 px right of the plot, so a press on a time label or in the legend
   starts a drag. Harmless, but the plot area would be the clearer hit
   area, now that the widget has bands above and below it.
6. **Suggestion (nit):** `std::vector<size_t> shown` in `draw_ticks()`
   (`Activity.cpp:336`) is unused.

Tests added to `test/GraphDataTest.cpp`:
`test_average_restarts_when_the_clock_goes_back` (fails, finding 2),
`test_repeated_timestamps_are_averaged`, `test_cadence_is_as_recorded`,
`test_columns_take_every_record_once`,
`test_time_ticks_after_the_specs_pause`, `test_ticks_at_the_edges` and
`test_legend_range_while_comparing` (all pass). The full run fails only on
`graph_data_test`, because of finding 2.
