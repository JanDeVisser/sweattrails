# Critical pace

Status: Done

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

The user sees what they can sustain on foot: their best times over the
distances runners race, and their **critical pace**, the pace they could
hold for a long time. They can see how it changes through a season, as
the power curve shows it for rides (`34-critical-power.md`), which this
builds on.

## Feature

### The method

- **Critical speed** (CS) comes from the same two-parameter model as
  critical power, with distance for work: an all-out run of `t` seconds
  covers `D = CS × t + D'`. CS, the slope, is shown as a pace, min/km;
  **D'**, the intercept, in metres, is the distance that can be run
  faster than CS before running out. It lands close to the pace of an
  hour's race, between 10 km and half-marathon pace.
- **The bests are by distance**, as runners race: a run's fastest time
  over each of a fixed list of distances, from 400 m to the marathon.
  Twenty minutes at 4:30/km means little to a runner; a 5 km in 21:34
  does. The model fits either way: each best is a point of a time and a
  distance.
- The fit is over the bests whose times are 3 to 20 minutes, as for
  power. As every run contains bests over the distances shorter than
  it, a run of 20 minutes or more in the window gives a fit, as a ride
  of 20 minutes does, if it wasn't all at one pace: an even run has a
  D' of about 0, and no fit, as a flat power curve has none.
- **Pace as run**, without adjusting for hills: a hilly run only sets
  slower bests. A long downhill could set a fast short best, but that's
  rare. Grade-adjusted pace is left out *(deferred)*: it needs a model
  of the grade's cost and clean altitude, and is a feature of its own.

### Which runs

- Outdoor runs count, trail runs too. Treadmill and indoor runs
  (`sub_sport` treadmill or indoor running) don't: their distance is
  the watch's estimate, often 5 to 10% off, and would set false bests
  for good.

### Which seconds

- The distance is the device's, from the records, which a foot pod
  feeds if there is one. A run without distances in its records gets no
  curve.
- **Time runs on through a stop**, a traffic light or an auto-pause, as
  it does for power: a 5 km best with 30 seconds at a light in it
  includes them. It's the same rule as the power curve's, and a best
  can't span a coffee stop. It lowers the bests of city runs a little.
- **GPS glitches**: distance gained between two records faster than
  **8 m/s** (2:05/km, faster than the 400 m world record's pace)
  doesn't count: the time does, the distance doesn't. A jump of 100 m in
  a second would otherwise set an impossible 400 m best for good. 8 m/s
  is a constant, as 1500 W is for power; a setting is left out
  *(deferred)*.

### The distances

- A run's bests are kept for a fixed list of distances, closer together
  at the short end, from 400 m to the marathon, with the table's among
  them. The list is the Developer's to choose, as it was for the power
  curve's durations.

### On the summaries screen

- **P shows the curve of the screen's sport**: power for cycling, as
  now, pace for running. **R and C switch between them** in the view,
  as they switch the bars. This changes `34-critical-power.md`, where R
  and C don't change the power curve.
- Everything else is as for power: the 90 days up to today, Left and
  Right by 4 weeks, back to the first outdoor run with distances, the
  all-time best behind, faint, Up, Down and Enter on the table, and P,
  F, G, W, M and Y leaving it.
- **The chart**: pace up the side, in min/km, as runners read it, so
  lower is faster: the curve rises as the distance grows, and the
  all-time best lies below the 90 days'. Distance runs along the bottom
  on a logarithmic scale, from 400 m to the longest run. The scale reads
  "4:00", "4:30", which wants steps of whole seconds that make sense as
  a pace, such as 10, 15 or 30 s, rather than 20 or 50.
- **The heading**: "Pace · CS 4:12 /km · D' 210 m · 90 days to 12 Oct
  2026". There's no threshold pace to show next to it, as there's no
  such setting. Without a sound fit, CS and D' are left out, as CP and
  W' are; without any outdoor run in the window, "Pace · no runs · 90
  days to 12 Oct 2026".
- **The table**: a row each for 400 m, 1 km, 1 mile, 5 km, 10 km, the
  half marathon and the marathon, for the distances a run in the window
  covered: "5 km · 21:34 · 4:19 /km · 12 Oct 2026 · Morning run". Enter
  opens the run. Predicted race times from CS and D' are left out
  *(deferred)*: the two-parameter model is too fast beyond about half
  an hour.

### What there is now

- The power view on the summaries screen (P, `34-critical-power.md`):
  the window of 90 days, Left and Right by 4 weeks, the all-time curve
  behind, the heading with the fit, the table with Enter opening the
  ride, a log time axis, and the curves in the summary file, version 3.
  This feature reuses all of that.
- `storage/PowerCurve.*` lays the records out a second each and slides
  windows over them; `critical_power()` is the least-squares fit, which
  takes watts and is the same arithmetic for metres per second.
- Records have the cumulative distance the device worked out, and a
  speed. A summary has a power curve for any activity with power, runs
  too (from a Stryd, or a watch's running power), but no pace curve.
- There's no threshold pace in `settings.json` (`29-fitness.md`
  deferred rTSS), so there's nothing to show next to CS as FTP is shown
  next to CP.

### The summary file

- Each outdoor run gets its bests by distance in the summary file, so
  the file's version goes up again, and it's built again once, reading
  every FIT file with its records, as it was for the power curve.

### README

The README's power curve section gets the pace curve: what CS and D'
are, P on running, R and C in the view, and which runs count.

### Version 1 scope

Everything above. Deferred:

- Grade-adjusted pace *(deferred)*.
- The glitch limit as a setting *(deferred)*.
- Predicted race times *(deferred)*.
- A threshold pace setting, and rTSS from it (`29-fitness.md`)
  *(deferred)*.
- The power curve of runs, from a Stryd or a watch's running power
  *(deferred)*: R in the power view shows pace.
- A run's own bests on the activity screen *(deferred)*.

## Open questions

None.

## Architecture

### Where the spec leaves a gap

- **When the fit is sound.** By distance, "a run of 20 minutes" can't be
  read from the bests: a 20-minute run of 4.7 km has no 5 km best, and
  its longest, 4 km, took 17 minutes. So the fit is sound if at least 3
  bests took 3 to 20 minutes, the longest of them 15 minutes or more,
  and CS and D' come out above zero. A run of 20 minutes or more gives
  that, unless it was all at one pace, which has a D' of about 0.
- **The distances** are `PaceDistances`: 400, 800 m, 1, 1.5 km, 1 mile,
  2, 3, 4, 5, 8, 10, 15 km, 10 miles, 20 km, the half marathon, 25, 30
  km and the marathon.
- **A best's time** is in whole seconds, and its start is interpolated
  between the two records it falls between, so that a 400 m best isn't
  a second or two slow for the records' spacing.
- **The glitch rule** is per pair of records: if the distance between
  them was gained faster than 8 m/s, or went back, none of it counts.
  Records without a distance are passed over.
- **The x axis** runs from 400 m, the first distance, to the longest of
  `PaceDistances` the longest run covered, as the power curve's runs to
  a listed duration.

### The pace curve (`storage/PaceCurve.*`), pure, next to `PowerCurve.*`

- **`PaceDistances`**, **`MaxRunSpeed = 8`** (m/s), and
  **`pace_curve(std::span<record const>)`**: a run's best time, in
  seconds, for each of the distances it covered, `std::vector<u32>`.
  It works on the records, not a second each as the power curve does:
  the cumulative distance that counts, and two indices sliding along it.
  Empty without distances.
- **`PaceBest { seconds, summary }`** and
  **`best_pace_curve(summaries, first, last)`**: the fastest of the
  outdoor runs between the days, as `best_curve()` is for rides.
  Treadmill and indoor runs are left out here, by `sub_sport`, not when
  the summary is read, so a change to the rule needs no rebuild.
- **`CriticalSpeed { cs, d_prime }`** (m/s, m) and
  **`critical_speed(bests)`**, with the rule above.
- **`fit_line(points)`**, the least squares that `critical_power()`
  does now, moves to `PowerCurve.h` for both to use.

### The summary file (`storage/Summaries.*`)

- `ActivitySummary` gets **`best_times`**, `std::vector<u32>`, for each
  of `PaceDistances` the run covered; empty for anything but a run. In
  the file as `"best_times"`, left out when empty.
- `read_summary()` works it out from `activity.recorded`, for runs only
  (`volume_sport()` is running, treadmills too: see above).
- `SummariesVersion` goes to 4.

### The chart (`src/Lib/Widget/Chart.*`, `LineChart.*`)

- **`nice_span(min, max, ticks, steps)`**: a scale from a step at or
  below `min` to one at or above `max`, in the first of `steps` that
  makes about `ticks` of them. Pace wants steps of 5, 10, 15, 30 s, 1,
  2, 5 min, not 1, 2 or 5 times a power of ten, and a scale that doesn't
  start at 0, where 0:00/km would squash the curve to the top.
- **`LineChart::steps`**: when it's set, `scale()` uses `nice_span()`
  over the lines' values; empty, as now. This is the widget library's
  only change.

### The screen (`widget/Summaries.*`)

- **`Graph::Power` becomes `Graph::Curve`**, the best power or pace by
  the view's sport, and `toggle_power()`, `power_end`, `step_power()`
  become `toggle_curve()`, `curve_end`, `step_curve()`. **R and C**
  change the sport in the view, which `set_sport()` already does, and
  the curve follows. `step_curve()` goes back to the first ride with
  power or the first outdoor run with best times, by the sport.
- **`refresh_pace()`** next to `refresh_power()`, with
  **`pace_heading()`**, **`pace_table()`**, **`pace_columns()`** and
  **`pace_rows()`**, pure: "5 km · 21:34 · 4:19 /km · 12 Oct 2026 ·
  Morning run". `PaceTableDistances`: 400 m, 1 km, 1 mile, 5 and 10 km,
  half and full marathon.
- **`curve_line()`** and **`curve_ticks()`** are generalized to take the
  x values (durations or distances) and the axis's first x, so that the
  pace curve shares them. Ticks: 400 m, 1, 5, 10 km, half, marathon.
- The table's rows are kept as the summaries they point to, for Enter,
  whichever the sport.
- The keys line: "P: best power or pace".

### The app (`SweatTrails.cpp`)

- The task becomes `st-summaries-curve`, still on P. The README.

## Test plan

- `PaceCurveTest` (new): `pace_curve()` of steady running, with the
  start interpolated; a faster kilometre in the middle; a stop counting
  as time; a glitch over 8 m/s and a distance going back not counting;
  records without distance; a run shorter than a distance.
  `best_pace_curve()` leaving out treadmill and indoor runs and rides,
  by day. `critical_speed()` of bests from a known CS and D', and none
  without 3 bests in range, without one of 15 minutes, or with D' at or
  below 0. `critical_power()` still passes its tests through `fit_line()`.
- `LineChartTest`: `nice_span()` for pace: 3:20 to 6:10 in steps of a
  minute, as 30 s would make more than 5; spans in steps of 10 and 15
  s; a flat line; `scale()` with `steps` set.
- `SummariesTest`: version 4 round trip with `best_times`; a version 3
  file is read as none; `read_summary()` of a run with distances, and
  none for a ride.
- `SummariesScreenTest`: P on running shows pace, R and C switch it; the
  heading with and without CS, and without runs; the rows and their
  pace; Left back to the first outdoor run; Enter opens the run; the
  power tests under the new names.
- By hand: the pace chart and its scale on Jan's runs, after the summary
  file is built again.

## Review notes

1. **Bug: Right gets stuck after R or C in an earlier window.**
   `step_curve()` (`widget/Summaries.cpp:743`) refuses any step whose
   end is before the first ride with power or outdoor run of the
   *current* sport, forward steps too. `set_sport()` (line 694) keeps
   `curve_end`. Steps to reproduce: rides with power from 2023 and runs
   from 2025. P on cycling, Left back to 2023, then R. The heading
   reads "Pace · no runs", and Right does nothing, since 2023 + 4 weeks
   is still before the first run. The only way out is P twice. C after
   going back in the pace curve does the same when rides start later
   than runs. Forward steps up to today should always be allowed, or
   `set_sport()` should clamp `curve_end` into the new sport's range.
   `test_pace_steps_forward_after_r` in `test/SummariesScreenTest.cpp`
   fails on it (lines 1650 and 1654). It is in `main()`, so
   `summaries_screen_test` fails until this is fixed.
2. **Suggestion: a summary file with more best times than
   `PaceDistances` reads past the array.** `decode()` takes
   `best_times` of any length. `best_pace_curve()` then returns a curve
   that long, and `critical_speed()` (`PaceCurve.cpp:126`),
   `pace_line()` (`Summaries.cpp:610`, and `log_line()`'s `xs`) and
   `refresh_pace()` (line 1046) index `PaceDistances` with it. The app
   never writes such a file, but a hand-edited one, or one from a
   build with a longer list under the same version, would cause
   undefined behaviour. `power` has the same issue with
   `PowerDurations`. Truncating in `decode()`, or treating the file as
   unreadable, would close it.
3. **Suggestion: "a run of 20 minutes or more gives a fit" isn't quite
   true for an even run.** For an evenly paced run, the line through
   its bests goes through 0, so D' comes out at 0, or just either side
   of it from rounding, and the fit is dropped. Real runs are seldom
   that even, so this hardly matters in practice. The Architecture
   section and the README promise it, though.
4. **Suggestion: two comments.** `PaceCurve.h:78`, "The bests of 3
   bests or more", should say what the constants are: at least 3 bests
   in the fit's range, the longest at least `PaceFitLongest`.
   `Summaries.h:139` calls `PaceTableDistances` "durations".

Checked and found sound: `covered()` and `fastest()`, which handle
records without distance, out of order or on the same second, glitches
at and over 8 m/s, a distance going back, a start above 0, stops at a
best's start or in it, the interpolated start at a record spacing of 7
s, and a run exactly as long as a distance. Also `critical_speed()` at
the edges (180 and 1200 s in, 1201 out, longest 899 vs 900),
`fit_line()` in `critical_power()`, `nice_span()`, and
`LineChart::scale()` with and without `steps`. `steps` is cleared for
the year to date and the fitness. `log_line()` and `log_ticks()` give
the power curve the same result as before. `outdoor_run()` and the
`curve_rides` behind Enter, also through `refresh_in_place()`, work as
specified.

Tests added by the Reviewer:

- `PaceCurveTest`: `test_out_of_order_and_repeated_seconds`,
  `test_the_glitch_limit`, `test_distance_from_a_start_above_zero`,
  `test_a_stop_at_the_start_of_a_best`, `test_critical_speed_bounds`.
- `SummariesScreenTest`: `test_pace_steps_cleared_for_the_other_lines`,
  `test_pace_steps_past_a_treadmill`, `test_pace_steps_forward_after_r`
  (fails, note 1), `test_pace_rows_over_an_hour`,
  `test_pace_refreshed_in_place`.
- `SummariesTest`: `test_read_best_times` reads a treadmill run too,
  which gets best times.

### Resolution

1. Fixed in 4973cee: the sport's first activity only holds Left back,
   and Right goes on up to today. `test_pace_steps_forward_after_r`
   fails against the old code, and is back in `main()`.
2. Fixed in b1670a7: the power curve and the best times are cut to
   `PowerDurations` and `PaceDistances` when the summary file is read.
   `SummariesTest`'s `test_curves_no_longer_than_their_lists` fails
   against the old code.
3. The rule stays, with Jan's agreement: a CS from even easy runs would
   be the easy pace. The README and this spec now say that a run all at
   one pace gives no fit (b36acc1).
4. Fixed in 8295f9e.
5. The tests were committed with the review notes, in 8992701.
