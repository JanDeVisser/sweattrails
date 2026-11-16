# Multisport activities

Status: In review, on branch feature/56-multisport

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

A triathlon or a duathlon recorded on a Garmin in multisport mode is one
FIT file with a session for each leg: swim, transition, bike, transition,
run. The user wants to look at each leg on its own, its map, data, laps
and graph, the way they look at a ride or a run, and step through the
legs as they step through the recordings of an activity
(`32-switching-recordings.md`), but without comparing them.

## Feature

### What there is now

Two of Jan's files, checked with `fitexplore`:

- **IM Ottawa**, 2025-08-03 (`2025/08/1754243172.fit`), a Garmin
  (product 4315): 5 sessions, swim 3.8 km 1:38:17 (8 laps), transition
  7:49, bike 179.5 km 5:20:05 (36 laps), transition 5:50, run 42.1 km
  6:16:58 (1 lap).
- **The duathlon**, 2026-06-28 (`2026/06/1782651592.fit`), the same
  Garmin: 5 sessions, run 4.9 km 24:30, transition 0:38, bike 20.0 km
  34:07, transition 0:36, run 2.7 km 13:20, one lap each. Next to it in
  the store: a Garmin warm-up run at 7:50 (2 min), a whole-race recording
  at 8:00 from a device FIT calls "development" (sport generic, 28 km
  1:13:11), and a Wahoo recording of the bike leg at 8:24 (20.1 km
  34:38). Its Strava title is "Morning Workout".

A scan of the store (2026-11-16) found **81** FIT files with more than
one session, from 2012 on, by their legs' sports:

| Legs                                      | Files |
|-------------------------------------------|------:|
| Run, T, Bike, T, Run (duathlon)           |    28 |
| Swim, T, Bike, T, Run (triathlon)         |     5 |
| Swim, T, Bike (and other partial races)   |     4 |
| Bike, Run (brick, no transition)          |    14 |
| Run, Bike                                 |     8 |
| Bike, T, Run                              |     6 |
| Run, generic; generic, Run; and the like  |    10 |
| Other mixes                               |     6 |

So many have no transitions, and some have legs of sport `Generic`: a
3-minute one between two runs, or a 2:18 one of 2.6 km after a swim.

In all of them, every session message has its own sport; none says
`Multisport`, and the `activity` message's `num_sessions` is 5.
`Activity::load_fit_file_filter()` overwrites the segment, the sport and
the sub-sport with each session, so **the last leg wins everywhere**:

- The inbox files the activity by the last leg's start: the duathlon is
  `1782651592`, 8:59, not 8:00 (`52-strava-export-other-recordings.md`
  already works around this).
- It shows as a run, with the running icon; the triathlon icon for
  `sport::Multisport` (`06-icons.md`) is never reached. That spec's
  review left "how multisport should work" for later: this is it.
- The summary line and the data panel are the last leg's: IM Ottawa is a
  42.1 km run of 6:16:58. The map, the graph and the laps are the whole
  file's, as they come from the records and lap messages.
- The summary file (`Summaries.h`) has one sport, distance and time per
  activity, so the totals (`27-volume-summaries.md`) count IM Ottawa as
  a 42.1 km run, and its swim and bike not at all; the duathlon as a
  2.7 km run. The fitness, power curve and pace curve are off the same
  way.
- Strava splits a multisport activity into one activity per leg, so the
  file's Strava title is one leg's: IM Ottawa's is "IM Ottawa Swim".
- The duathlon isn't grouped with its Wahoo bike recording
  (`31-multiple-recordings.md`), as it loads as a run.

### Two specs

The feature is split in two:

1. **This spec**: loading a multisport file as one multisport activity,
   and stepping through its legs on the activity screen.
2. A follow-up *(to be written)*: the summary file, and with it the
   totals, the fitness and the power and pace curves, counted per leg:
   IM Ottawa's swim as a swim, its bike as a ride, its run as a run.
   Filing the activity by its first leg's start, which renames files in
   the store and touches the Strava import's workaround, goes there too,
   or is left out.

### A multisport activity

- A FIT file is **multisport** if it has more than one session. The
  `activity` message's `num_sessions` isn't relied on, as it can be
  missing.
- Each session is a **leg**, with its own sport, start, elapsed and
  moving time, distance and ascent, and its laps (the session's
  `first_lap_index` and `num_laps`, or, without them, the laps that start
  within it). Its records are those from its start to the next leg's.
- Transitions are legs too, with FIT's sport `Transition`. A file
  without them (a brick: Bike, Run) is multisport all the same.
- A leg of sport `Generic` is a leg like any other, named "Other".
- The **activity as a whole** is `sport::Multisport`, so it shows the
  triathlon icon in the header and the month's list. Its start is the
  first leg's, its distance, elapsed and moving time and ascent are the
  legs' sums, and its summary line says so: IM Ottawa is "226.0 km ·
  13:28:59 · …", not the run's.
- Where it's filed doesn't change in this spec: the inbox still files it
  by the last leg's start, as it does now.
- **Recordings**: a multisport activity is never grouped with other
  recordings (`31-multiple-recordings.md`), in either direction: the
  Wahoo's recording of the duathlon's bike leg stays an activity of its
  own. Grouping a leg with a recording of it is a feature of its own
  *(deferred)*.

### The tabs

- A multisport activity has a **Whole** tab, then one tab per leg that
  isn't a transition, in order: "Swim · Bike · Run" for IM Ottawa, "Run
  · Bike · Run" for the duathlon.
- A **tab strip** under the title shows the tabs, the one shown
  highlighted: `Whole  Swim  [Bike]  Run`. Clicking a tab shows it. It's
  there for multisport activities only: recordings keep saying which one
  is shown on the summary line, as now. Nothing like it is in
  `src/Lib/Widget` yet; it belongs there, as a small widget of its own.
- Transitions have no tab: they're only the Whole tab's laps (below).
  Most last under a minute and have little to show on their own.
- **Tab** shows the next tab, **Shift+Tab** the one before, wrapping
  around, as they switch recordings now. On a multisport activity they
  only step through the tabs; as it has no recordings, that doesn't
  clash.
- The activity opens on **Whole**. Left, Right and Ctrl+M work as from
  the activity, and coming back to it shows Whole: the tab shown isn't
  remembered.

### The Whole tab

- What the activity screen shows now, with the whole activity's
  numbers: the map of the whole route, the data panel and the graph over
  the whole file.
- Its **laps are the legs**, transitions included, named by their sport:
  "Swim 1:38:17 · T1 7:49 · Bike 5:20:05 · T2 5:50 · Run 6:16:58", the
  split table of a race result. Selecting one selects that stretch, on
  the map and the graph, as a lap does now. The legs' own laps are on
  their tabs.

### A leg's tab

- Everything the activity screen shows, for that leg only, as if it were
  an activity of its own: its map, its data panel (by the leg's sport, so
  the bike shows speed and the run pace), its laps, and its graph.
- The summary line says which leg: "179.5 km · 5:20:05 · … · Bike, leg 2
  of 3". With a lap or range selected, their line says it too, as it
  says the recording now.
- The **title** is the activity's on every tab.
- **What a step keeps**: nothing. The map fits the leg, and a selected
  lap or range is dropped, as the legs share neither.
- A swim leg shows what a swim shows now (pace in min/km). A pace per
  100 m is for swims in general *(deferred)*.

### Keys on a leg's tab

- **F2** renames the activity, and the description is the activity's,
  as on Whole.
- **Laps** (Ctrl+L, Ctrl+Shift+L, Ctrl+Alt+L), **filling a gap**,
  **Ctrl+Shift+F**, and the **overlay** (Ctrl+O, Ctrl+Shift+O, Ctrl+E)
  work on the FIT file, so only on Whole. On a leg's tab they say "Not on
  a single leg: press Tab to go to Whole."
- Laps the user made on Whole replace the device's laps there, as now;
  the legs' tabs keep showing the device's laps of each leg
  *(user laps per leg deferred)*.
- **Ctrl+J** and **Ctrl+Shift+R** say "This activity has one recording."
  on any tab, as there are none.

### README

The README says that multisport files show as one activity with a tab
per leg, that Tab and Shift+Tab step through them, and that the legs are
the laps of the Whole tab.

### Version 1 scope

Everything above. Deferred:

- Totals, fitness and curves per leg, and filing by the first leg's
  start: the follow-up spec.
- Grouping a leg with another device's recording of it *(deferred)*.
- User laps per leg *(deferred)*.
- Swim pace per 100 m *(deferred)*.
- Per-leg Strava titles: the activity keeps the one title it has
  *(deferred)*.

## Open questions

None.

## Architecture

### Loading: legs in `Activity`

- `storage/Activity.h` gets a `struct Leg`: the session's sport and
  sub-sport, a `Segment` (start, end, elapsed, moving, distance, ascent
  from the session, the rest from `analyze()` over its records as
  recorded), `first_record`/`end_record` into the activity's records, as
  `Lap` has, the session's `first_lap_index`/`num_laps` if it has them,
  and its own laps (`std::vector<Lap>`, indices into the activity's
  records).
- `Activity` gets `std::vector<Leg> legs`: one per session, on any load.
  `is_multisport()` is `legs.size() > 1`.
- `load_fit_file_filter()` no longer overwrites the segment and sport per
  session: it appends a `Leg`. After the file is read, `load_fit_file()`
  makes the activity's sport and segment from the legs: from the one leg
  as now, or for several, `sport::Multisport`, no sub-sport, the first
  leg's start, the last leg's end, and the sums of elapsed, moving,
  distance and ascent (no ascent if no leg has one). A workout message's
  sport doesn't override Multisport.
- `analyze()`, on a multisport activity, gives each leg its records (its
  start up to the next leg's start), its laps (by `first_lap_index` and
  `num_laps` when they're there and in range, else the laps starting
  within it), and its segment stats. Then the activity's `laps`, and so
  `device_laps`, are the legs as laps, titled by `leg_name()`. The user's
  laps on Whole replace them as they replace the device's laps now, and
  Ctrl+Alt+L goes back to them.
- `Activity` also gets `std::optional<LegShown> leg`, set on an activity
  that is one leg of a multisport one: which leg (its number among the
  legs that aren't transitions, and their count) and its name. The
  summary line and the key checks use it.

### `storage/Legs.{h,cpp}` (new)

Free functions on a loaded `Activity`, all testable without a window:

- `leg_name(Activity const &, size_t leg)`: "Swim", "Bike", "Run",
  "Other" for Generic, "T1", "T2", ... for transitions by their order,
  and FIT's name, spaced, for anything else ("Walking").
- `leg_tabs(Activity const &)`: the indices of the legs that aren't
  transitions, which are the tabs after Whole.
- `leg_activity(Activity const &whole, size_t leg)`: the leg as an
  activity of its own: the whole's names, id and sidecars; the leg's
  sport, segment, records and recorded slice, and its laps rebased to
  that slice as `laps` and `device_laps`; no legs, no user laps, and
  `leg` set. The rest of the screen (map, graph, data panel, ranges)
  works on it unchanged.
- `filing_start(Activity const &)`: the start the inbox files by: the
  last leg's for a multisport activity, else the start. The inbox
  (`Storage.cpp`) and the Strava export's `read_original()` use it, so
  that where a file is filed doesn't change.
- `tab_labels()` and `tab_activity()`: a multisport activity's tabs'
  labels, "Whole" first, and what each shows: the whole, or
  `leg_activity()` of its leg.
- `whole_segment()` and `split_into_legs()`: the loading's part, which
  `Activity.cpp` calls.

### The summary file

Unchanged until the follow-up spec: `summarize()` takes a multisport
activity's sport, sub-sport, start, distance, moving time and ascent from
its last leg, as the summaries in the cache have them, so the totals don't
change and there's no new version.

### Recordings

`same_activity()` (`Recordings.cpp`) is false when either is
`Multisport`, so that neither the grouping run nor Ctrl+J groups a
multisport activity, in either direction.

### Widget library: `TabStrip`

`src/Lib/Widget/TabStrip.{h,cpp}`: a row of labels, the selected one
highlighted, as a small drawable struct like `ActivityDataDisplay`'s
clickable fields rather than a `Widget` in a `Layout`, because it's drawn
inside the data panel, between the title and the notes:

- `labels`, `selected`; `select_next()`/`select_previous()`, wrapping;
- `draw(x, y)` draws it and keeps each tab's area;
- `tab_at(Vector2)` is the tab under a point, from those areas;
- `height()`.

`ActivityDataDisplay` owns one, adds a row for it under the title when
it has more than one label, and on a click calls back to the
`ActivityDisplay` to show that tab.

### Activity screen

- `ActivityDisplay` keeps the whole multisport activity
  (`std::optional<Activity> multisport`); the data panel's `TabStrip`
  says which tab is shown. `set_activity()` resets both: a multisport
  activity opens on Whole, and coming back to it does too.
- `show_tab(size_t)` shows Whole or `leg_activity()` of the tab's leg
  through the existing `show()`, without keeping the view, so the map
  fits the leg and no lap or range is kept. Leaving Whole, it keeps
  Whole as shown, with the user's laps; leaving a leg, it copies the
  names (title, notes) into the whole, so a rename on a leg's tab shows
  on the others. It closes the overlay preview on a leg.
- `step_tab(int)` for Tab and Shift+Tab: `cmd_next_recording` and
  `cmd_previous_recording` step tabs on a multisport activity, and
  recordings otherwise.
- The summary line (`DataFields.cpp`): `activity_summary()`,
  `lap_summary()` and `range_summary()` end with ", Bike, leg 2 of 3" on
  a leg, as they end with the recording now. A lap with a title (a leg
  on Whole) is named by it: "T1 · 7:49 · …" rather than "Lap 2 of 5". The
  lap table's first column shows the title where there is one.
- Key checks (`SweatTrails.cpp`): the lap keys, filling a gap,
  Ctrl+Shift+F and the overlay keys say "Not on a single leg: press Tab
  to go to Whole." when `activity.leg` is set; Ctrl+J says "This activity
  has one recording." on a multisport activity, and so does Ctrl+Shift+R,
  unless it has other recordings from before this spec, grouped when it
  loaded as its last leg: then it still splits them, as the way out. Tab
  always steps through the tabs on a multisport activity.

## Test plan

- `LegsTest.cpp` (new), with FIT files made by `TestFit.h`'s
  `FITBuilder`, extended with a lap message and a session's lap index:
  - a five-session triathlon loads as Multisport, with the first leg's
    start, summed distance, elapsed, moving and ascent, and five legs
    with their sports, records and laps (by lap index, and without it by
    start);
  - a brick without transitions is multisport; one session isn't;
  - a Generic leg is named "Other"; transitions "T1", "T2";
  - the legs are Whole's laps, titled, and are the device laps that the
    user's laps replace and Ctrl+Alt+L brings back;
  - `leg_tabs()` leaves out transitions;
  - `leg_activity()`: the leg's sport, segment, records, rebased laps,
    no user laps, `leg` set, the whole's names;
  - `filing_start()` is the last leg's start, and the inbox files a
    multisport file there.
- `RecordingsTest.cpp`: `same_activity()` is false for a multisport
  activity and a recording of one of its legs, both ways.
- `TabStripTest.cpp` (new): wrapping both ways, `tab_at()` from set
  areas.
- `DataFieldsTest.cpp`: the summary, lap and range lines on a leg, and
  a titled lap's line and table cell.
- `SummariesTest.cpp`: a multisport file's summary is its last leg's.
- By hand, in the app: IM Ottawa and the duathlon: the icon, the tabs,
  Tab and clicking, the map fitting each leg, the blocked keys.

## Review notes

*Filled in by the Reviewer.*
