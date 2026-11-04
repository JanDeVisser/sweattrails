# Critical pace

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
  of 20 minutes does.
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

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
