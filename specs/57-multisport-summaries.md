# Multisport activities in the summaries

Status: In progress, on branch feature/57-multisport-summaries

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

`56-multisport.md` loads a multisport FIT file (a triathlon, a duathlon,
a brick) as one activity with a leg per session. This is its follow-up:
the summaries screen counts each leg as what it is. IM Ottawa's bike
counts as a ride and its run as a run; the duathlon's two runs count as
running and its bike as cycling. The fitness and the power and pace
curves take each leg on its own.

## Feature

### What there is now

The summary file (`storage/Summaries.h`) has one entry per FIT file.
For a multisport file, `read_summary()` takes the sport, start,
distance, moving time and ascent of its **last leg**, as before spec 56,
but NP, the heart rate load and the power and pace curves of the
**whole file's** records. With 81 multisport files in the store (28 of
them duathlons, see spec 56), that means:

- **Totals**: IM Ottawa counts as a 42.1 km run; its 179.5 km ride
  isn't counted. The duathlon counts as a 2.7 km run; its first run and
  its bike aren't counted.
- **Pace curve**: a file whose last leg is a run counts as a run, with
  the pace curve of all its records, the bike leg's included. A
  duathlon's 20 km bike at 35 km/h sets "running" bests at every
  distance it covers. This is a bug now, not just a gap.
- **Power curve**: a file whose last leg is a run doesn't count, so IM
  Ottawa's and the duathlons' bike power is missing.
- **Fitness**: the training stress takes NP (or the heart rate load) of
  the whole file but the moving time of the last leg, and the last leg's
  sport for its LTHR: wrong both ways.

### Counting legs

- Each **leg** of a multisport activity counts in the summaries as an
  activity of its own sport would: its start, distance, moving time,
  ascent, NP, heart rate load, power curve and pace curve are the leg's,
  from its own records.
- **Totals** (`27-volume-summaries.md`, `28-volume-year-to-date.md`):
  a run leg adds to running, a bike leg to cycling, by the leg's sport
  and sub-sport as for any activity. Swim, transition and "Other" legs
  count in no total, as swims and generic activities don't now.
- The **count** counts an activity once per total it adds to: the
  duathlon is one run and one ride, not two runs, as the count is "how
  many times did I run", and a duathlon is one race. Its distance, time
  and ascent are both run legs' all the same.
- **Fitness** (`29-fitness.md`): every leg has its training stress, by
  power with the FTP if it has power, else by heart rate with the LTHR
  of its sport (cycling's for the bike, running's for everything else,
  as now). The day's TSS is the sum of the legs', as of any two
  activities on a day. Transitions count too: they're short, and real
  effort, and add a point or two.
- **Power curve** (`34-critical-power.md`): a bike leg counts as a
  ride. A run leg with power (a Stryd) doesn't, as runs don't now.
- **Pace curve** (`35-critical-pace.md`): a run leg counts as an
  outdoor run, with its own records only. The duathlon's first and
  second runs are two runs.
- A file with one session is summarized as now.

### Opening a best

Clicking a best on the power or pace curve opens the activity that set
it, as now. For a leg's best, it opens on **that leg's tab**, not on
Whole, so that what's shown is what set the best. The curve's table
names it by the activity's title and the leg's name: "IM Ottawa · Bike".
A best of an activity with one session is named and opened as now.

### Where it's filed

The inbox keeps filing a multisport activity by its last leg's start,
and the store's files keep their names. Filing by the first leg's start
would rename files in the store, and change the Strava import's
workaround (`52-strava-export-other-recordings.md`) and the recordings'
start (`Recordings.cpp`), for little gain: the activity's start is
already the first leg's (spec 56), and only the file's name says
otherwise. Each leg's
summary is dated by the leg's own start.

### The summary file

The summaries are built again once, as the version changes: that takes
the minutes it took when spec 34 and 35 changed it. The status bar's
"1,234 activities" keeps counting FIT files, not legs.

### Version 1 scope

Everything above. Deferred:

- Totals for swimming: swims, and swim legs, count in no total yet
  *(deferred)*.
- Filing a multisport activity by its first leg's start: left out (see
  above).

## Open questions

None.

## Architecture

### The summary file: a summary per leg

- `ActivitySummary` gets `std::optional<u32> leg`, the leg's index in
  the activity's `legs`, and `leg_name` ("Bike", "Run", "T1"), both
  empty for a file with one session. The name is kept so that the
  curves' table can say "IM Ottawa · Bike" without loading the file.
- `read_summary()` becomes `read_file_summaries(Storage &, SummaryFile)`,
  which gives a summary per leg, each made from `leg_activity()` of
  its leg as a single activity's is made: the leg's sport, its own
  start, distance, moving time, ascent, NP, heart rate load and curves.
  A file with one session, or that can't be read, gives one summary.
- `plan_update()` keeps all of a file's summaries if it hasn't changed.
- `SummariesVersion` goes to 5, so the file is built again once.

### Counting files, not legs

- `store_summary()` counts the files that have a sport.
- The totals' count (`volume_totals()`, `year_to_date()`): a leg
  doesn't count when the summary of that sport before it is of the same
  file. Summaries are in order of start, so a duathlon's two runs are
  next to each other among the runs.
- `without_stress()` counts a file once, when none of its legs has a
  training stress: an Ironman's swim without heart rate doesn't make it
  count as rest.

### Unchanged

The fitness and the curves take the summaries as they come: the day's
TSS adds up the legs', transitions too, and `best_curve()` and
`best_pace_curve()` take bike and run legs as rides and runs.

### Opening a best

- The curves' table names a leg's best by the activity's name (cached
  per file, as now) and " · " and its `leg_name`.
- `on_ride` (`SweatTrails.cpp`) shows the activity, and for a leg's
  best its tab: `tab_of_leg()` (new, `storage/Legs.h`) is the leg's tab
  number, 0 (Whole) for a leg without one.

## Test plan

- `SummariesTest.cpp`: a triathlon made with `FITBuilder` gives a
  summary per leg, each with its sport, start, distance, NP, heart rate
  load and curves from its own records: a run leg has no bests from the
  bike's records. `leg` and `leg_name` are written and read back;
  `plan_update()` keeps all of a file's legs; `store_summary()` counts
  files.
- `VolumeTest.cpp`: a duathlon counts as one run and one ride, with
  both runs' distance; also in the year to date.
- `FitnessTest.cpp`: the day's TSS adds up the legs'; `without_stress()`
  counts a file once, and not if a leg has stress.
- `PowerCurveTest.cpp`, `PaceCurveTest.cpp`: a bike leg's best is on the
  power curve, a run leg's on the pace curve.
- `LegsTest.cpp`: `tab_of_leg()`.
- `SummariesScreenTest.cpp`: a leg's best is named "Title · Bike".
- By hand: IM Ottawa's and a duathlon's totals, and clicking a leg's
  best opens its tab.

## Review notes

*Filled in by the Reviewer.*
