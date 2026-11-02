# Critical power

Status: Ready

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

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
