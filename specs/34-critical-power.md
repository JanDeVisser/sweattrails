# Critical power

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

The user sees what they can sustain on the bike: the best power they've
held for each duration, and their **critical power** (CP). They can see
how it changes through a season, and check the FTP they set in
`settings.json` (`29-fitness.md`) against what they actually rode.

Runs get the same, as critical pace, in a spec of its own that builds on
this one *(deferred)*: running has its own problems, such as GPS speed,
treadmills and hills, and power and pace together would be too big for
one spec.

## Feature

### The method

- The **mean-maximal curve** of a stretch of training has the best
  average for each duration, from 1 s to the longest activity: the best
  5 s, 1 min, 5 min, 20 min, 1 h of power.
- **Critical power** comes from the two-parameter model: the work done
  in an all-out effort of `t` seconds is `W = CP × t + W'`. Fitting a
  line through the best efforts of about 3 to 20 minutes gives CP, the
  slope, in W, and W', the intercept, in kJ: the work that can be done
  above CP before running out.
- Critical pace, for runs, is the same model with distance for work:
  `D = CS × t + D'`. It's for the next spec.
- CP lands close to FTP, a little above it. The app shows them side by
  side, and doesn't set the FTP from CP: the user changes
  `settings.json` if they agree with it. Suggesting an FTP is left out
  *(deferred)*.

### What there is now

- An activity's records have power, speed and heart rate per second,
  and `Segment::analyze()` already works out NP with
  `NormalizedRange<u16, 30>` (`storage/Activity.h`).
- The summary file (`summaries.json`, `27-volume-summaries.md`) has a
  line per activity, of its main recording only (`31-multiple-recordings.md`),
  with NP and `hr_load` since version 2, read with the records. There's
  no mean-maximal curve of an activity in it yet: a curve over a season
  needs one per activity, so the file grows and is built again.
- The summaries screen (Ctrl+T) has three views, the bars, the
  year-to-date lines (G) and the fitness (F), with a line chart that
  this could use.
- The thresholds in `settings.json` are dated: FTP and LTHR. There's no
  threshold pace (`29-fitness.md` deferred rTSS).

### Which rides, and which seconds

- Every ride with power counts: on the road, on the trainer and in
  Zwift. A trainer may read a few percent off a road power meter, but
  leaving indoor rides out would leave winters nearly empty.
- Readings above **1500 W** count as zero: a dropout or a bad pairing
  reads as a spike, which would spoil the 1 to 5 second bests, and the
  all-time curve for good. 1500 W is a constant for now; making it a
  setting is left out *(deferred)*.

### Pauses

- Time runs on through a stop inside a ride, an auto-pause or a café,
  at zero power: an effort can't be stitched together from before and
  after a stop. A 20-minute best that spans a coffee stop would
  overstate it. Seconds without power inside a stretch of recording
  count as zero too.

### Recordings

- The curve is of each activity's main recording, as the totals are
  (`31-multiple-recordings.md`). If the main recording of a ride has no
  power and another one has, the ride doesn't count: picking the
  recording with the power meter as the main one (Ctrl+J, or the
  `prefer` setting) fixes that.

### On the summaries screen

- The power curve is a view of the summaries screen (Ctrl+T), next to
  the bars, the year-to-date lines and the fitness. **P** shows it, and
  P again, F, G, or W, M and Y leave it, as F does for the fitness.
- **The window** is the 90 days up to today, as intervals.icu and
  Golden Cheetah have it by default, so that CP is of current form.
  **Left and Right** move it 4 weeks back or forward, back to the first
  ride with power, and forward to today.
- **The chart**: the window's curve, bold, and behind it, faint, the
  all-time best curve, of every ride there is, so that the user sees how
  far today's form is from their best at each duration. Time runs along
  the bottom from 1 s to the longest ride, on a logarithmic scale: the
  first minute would be a sliver otherwise.
- **The heading**: "Power · CP 288 W · W' 18 kJ · FTP 275 W · 90 days
  to 12 Oct 2026", with the FTP in force on the window's last day, or
  without it if there's none.
- **Without a sound CP** the curve still shows, and the heading leaves
  CP and W' out: "Power · FTP 275 W · 90 days to 12 Oct 2026". A fit is
  sound if the window has best efforts across 3 to 20 minutes and both
  CP and W' come out above zero; a CP from a window of easy rides would
  be low and misleading. A window without any ride with power says so:
  "Power · no rides with power · 90 days to 12 Oct 2026", with an empty
  chart, but the all-time curve still behind it.
- **The other keys**: R and C, and D, T, N and A, don't change the
  power curve, which is of rides, in watts, as they don't change the
  fitness. They change the view underneath, which shows when the user
  leaves. (In the critical pace spec, R and C may switch between pace
  and power.)
- **The table**: a row each for 5 s, 15 s, 30 s, 1, 2, 5, 10, 20, 30
  and 60 minutes, and 2 hours: the window's best power for that long,
  and the date and name of the ride that set it. Up and Down go through
  the rows, and Enter opens that ride on the activity screen. Elsewhere
  on the summaries screen Enter drills down into a period, so this is
  new: there's no period to drill into here. The summary file has no
  titles, so the names of the table's rides are read when it's built,
  as the activity list reads them.

### The summary file

- A curve over 90 days needs each ride's own curve, so the summary file
  gets one per ride with power: its best power for a fixed set of
  durations, from 1 s to the ride's length, spaced more closely at the
  short end. The set is the Developer's to choose; it has to include the
  table's durations and enough points for the chart and the fit.
- The summary file's version goes up, so it's built again once, reading
  every FIT file with its records, as it was for the fitness: it takes
  minutes, in the background, with its progress on the status line.

### README

The README says what the power curve, CP and W' are, the P key, and
how CP compares with the FTP in `settings.json`.

### Version 1 scope

Everything above. Deferred:

- Critical pace for runs, in a spec of its own *(deferred)*.
- Suggesting an FTP from CP *(deferred)*.
- The spike limit as a setting *(deferred)*.
- An activity's own curve or bests on the activity screen *(deferred)*.
- Leaving a ride out of the curve by hand *(deferred)*.
- Power per kilo, which needs the rider's weight *(deferred)*.

## Open questions

None.

## Architecture

### Where the spec leaves a gap

- **Gaps between records.** Garmin's smart recording writes a record
  every few seconds while riding, not every second, so a gap isn't a
  pause. A gap of up to `RangeMovingGap` (10 s), the moving gap of
  ranges and `hr_load`, takes the power of the record after it, as if
  held; a longer gap is a pause, at zero.
- **The durations** are a fixed list, `PowerDurations`: 1, 2, 3, 5, 8,
  10, 15, 20, 30, 45 s, 1, 1.5, 2, 3, 4, 5, 7, 10, 12, 15, 20, 25, 30,
  40, 50, 60, 75, 90 min, 2, 2.5, 3, 4, 5, 6, 8 and 10 h. Roughly
  geometric, so the chart and the fit have points at every scale, and
  they include the table's.
- **The fit** is least squares of `W = P × t` against `t` over the
  window's bests at 3, 4, 5, 7, 10, 12, 15 and 20 minutes. "Best efforts
  across 3 to 20 minutes" means the window has a best for all of them,
  that is, a ride of 20 minutes or more.
- **Left and Right**: the window's last day moves 4 weeks, forward up to
  today, and back as long as the window still ends on or after the first
  ride with power; a step that would go past either does nothing. P
  opens on the window up to today.
- **The table's rows** for durations no ride in the window lasted are
  left out.
- **A ride's name** is read from its FIT file and sidecar, as on the
  activity screen, through a callback the app sets, and kept by file
  while the screen is open: the table is refreshed after each chunk of
  the summary job, and on each Left and Right.

### The curve (`storage/PowerCurve.*`), pure

- **`PowerDurations`**, the list above, and **`MaxPower = 1500`**.
- **`power_curve(std::span<record const>)`**: a ride's best average
  power for each duration it lasts, `std::vector<u16>`, as long as the
  durations shorter than the ride. It lays the records out a second
  each from the first to the last, with the gaps held or at zero as
  above, readings over `MaxPower` and records without power at zero,
  and slides a window of each duration over the running sums. Empty for
  records without any power. About 40 durations over 36,000 seconds for
  a 10-hour ride: fast enough to run in `read_summary()`.
- **`PowerBest { watts, summary }`**, the best and the index of the
  summary that set it, and **`best_curve(summaries, first, last)`**: the
  best for each duration over the cycling summaries (`volume_sport()`)
  that start between those days, by `start_day()`. Both the window's
  curve and the all-time curve are this, with other days.
- **`CriticalPower { cp, w_prime }`**, and
  **`critical_power(bests)`**: the fit above, none if it isn't sound.

### The summary file (`storage/Summaries.*`)

- `ActivitySummary` gets **`power`**, `std::vector<u16>`, the ride's
  curve; empty without power. It's in the file as an array, `"power"`,
  left out when it's empty.
- `read_summary()` works it out from `activity.recorded`, the records
  as recorded, not smoothed, for any activity with power: the pace spec
  can use the runs' power later if it wants.
- `SummariesVersion` goes to 3, so the file is built again.

### The screen (`widget/Summaries.*`)

- `Graph` gets **`Power`**, and `SummaryView` **`power_end`**, the
  window's last day. **`toggle_power()`** (P), as `toggle_fitness()`.
  `step_year()` moves the window in the power view.
- **`power_heading(window_bests, cp, ftp, end)`**, and
  **`power_columns()`** and **`power_rows(bests, summaries, name_of)`**,
  pure as the fitness's: "5 s · 812 W · 12 Oct 2026 · Morning ride".
- **The log scale** without changing `LineChart`: the curves are
  sampled at evenly spaced logarithms of the duration, interpolated
  between `PowerDurations`, so that x is the log of the time.
  **`curve_line(bests, points, longest)`** does this, pure, and the
  ticks are at 1 s, 10 s, 1, 5, 20 min, 1 h and so on, at their own
  logarithms. A ride's curve that stops before the all-time one stops
  partway, as this year's year-to-date line does.
- **Enter** in the power view calls **`on_ride(summary)`**, which the
  app sets to load the ride and show it, as `on_week` lists a week.
  **`ride_name(summary)`** is the app's too.

### The app (`SweatTrails.cpp`)

- Task `st-summaries-power`, P, `.when(on_summaries)`. The callbacks
  above. The README.

## Test plan

- `PowerCurveTest` (new): `power_curve()` of steady power, of an effort
  in the middle of a ride, a pause longer than 10 s at zero, a gap of 5
  s held, a spike over 1500 W at zero, a ride without power, a ride
  shorter than a duration; `best_curve()` over rides inside and outside
  the window, runs left out, the index of the summary that set each
  best; `critical_power()` of bests made from a known CP and W', none
  without a 20-minute best, none for a W' at or below zero.
- `SummariesTest`: version 3 round trip with `power`; a version 2 file
  is read as none; `read_summary()` of a FIT file with power has the
  curve.
- `SummariesScreenTest`: P and back, F, G and W from the power view;
  the heading with CP and FTP, without a sound CP, and without rides;
  Left and Right and their limits; R doesn't change it; the rows, with
  the durations no ride lasted left out; Enter calls `on_ride` with the
  row's ride; `curve_line()`'s samples at the durations themselves.
- By hand: the chart on Jan's rides, after the summary file is built
  again, and Enter opening a ride.

## Review notes

*Filled in by the Reviewer.*
