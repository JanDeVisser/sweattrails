# After-the-fact laps

Status: In progress, on branch feature/after-the-fact-laps

## Goal

The user gives an activity laps it didn't get while it was recorded: a
device that doesn't lap by itself gives one lap, the whole activity, and
a lap button forgotten during an interval session leaves the intervals
unmarked. Laps made afterwards show in the lap table, the strip and the
data panel as the device's laps do (`specs/lap-display.md`), so the
intervals can be compared side by side.

## Feature

### What there is now

- `Activity::laps` has a `Lap` per FIT lap message, with its records
  (`first_record`, `end_record`) and its `Segment`, whose distance and
  times come from the lap message and the rest from
  `Segment::analyze()`.
- `range_segment()` (`specs/range-stats.md`) works out a lap's stats for
  any range of records, from the records as recorded, distance and times
  included. A lap made afterwards can get its stats from it.
- A range dragged in the graph shows its stats in the data panel, but
  can't be kept: the next drag, Esc or another activity clears it.
- What SweatTrails knows about an activity beyond its FIT file goes in
  the sidecar, `<start>.json`, under keys of its own (`"title"`,
  `"strava"`), which keeps the rest of the sidecar: the FIT file is never
  written.

### Laps as boundaries

- The user's laps are kept as **boundaries**: the places where one lap
  ends and the next begins. The laps run from the start to the first
  boundary, from boundary to boundary, and from the last to the end, so
  they always cover the whole activity, as the lap strip and the table
  assume (`specs/lap-display.md`). There are no gaps that aren't laps.
- **They replace the device's laps.** An activity shows one set: the
  user's, if they've made any, else the device's. The table, the strip,
  Up and Down, and the data panel work on them as on the device's.
- The first change starts from the device's laps: their boundaries
  become the user's, and the change is made to those. An activity the
  device didn't lap starts from one lap, the whole activity.
- Each lap's stats come from its records, with `range_segment()`, as a
  dragged range's do: its distance and times from the records, not from
  a lap message, which a lap made afterwards doesn't have.

### Splits

- A task opens an input box, "Split every", which takes a distance in
  km or a time in minutes: "1 km", "5 km", "0.4 km", "10 min". The
  activity gets a boundary at each split, from the start, in place of
  all the boundaries it had: a split is a fresh set of laps. The last
  lap is what's left, however short.
- After a split, the whole activity is selected, and the table shows the
  new laps.
- Only km and minutes: the app shows no miles anywhere.
- An input it can't read says so, with an example, and changes nothing.
  So does a distance split of an activity without distance.

### Marking a range as a lap

- With a range dragged in the graph (`specs/range-stats.md`), a task
  makes it a lap: a boundary at each end of it. The lap the range was in
  becomes up to three: before the range, the range, and after it. A
  range across a boundary splits both laps it touches, and the
  boundaries inside it go: the range is one lap.
- A range end within 5 seconds of a boundary that's already there takes
  that one, rather than leaving a lap of a few seconds.
- Without a dragged range, or with a lap selected rather than a range,
  it says to drag a range in the graph first, and changes nothing.
- The lap it made is selected: the data panel shows the same stats as
  for the range, now as "Lap 2 of 3".

### Going back to the device's laps

- A task removes the user's boundaries, and the device's laps come back,
  with the whole activity selected.
- Removing a single boundary, such as by clicking it in the strip, is
  *(deferred)*.

### Where they're kept

- In the activity's sidecar, under SweatTrails' own key `"laps"`: the
  Unix time each lap after the first starts, earliest first, such as
  `"laps": [1793102760, 1793103100]`. The rest of the sidecar is kept,
  and an activity without a sidecar gets one, as for the user's title
  (`specs/edit-activity-name.md`). Going back to the device's laps
  removes the key, and the sidecar if nothing else is left in it.
- Times rather than record indices: they stay right if the records are
  ever read differently. A boundary at a time between two records is at
  the first record at or after it.
- A sidecar that can't be written leaves the laps as they were, and the
  status line says why, as a rename that fails does.

### Keys

Each is a task, for the command palette to come:

| Task             | Key          | Does                                     |
|------------------|--------------|------------------------------------------|
| `st-mark-lap`    | Ctrl+L       | Makes the dragged range a lap            |
| `st-split-laps`  | Ctrl+Shift+L | Opens "Split every", and splits the laps |
| `st-device-laps` | Ctrl+Alt+L   | Goes back to the device's laps           |

They do nothing under a modal. Ctrl+Alt is Ctrl+Option on macOS.

### Version 1 scope

- Laps as boundaries, which replace the device's laps.
- Splits by km or minutes.
- Marking a dragged range as a lap.
- Going back to the device's laps.
- *(Deferred)*: removing one boundary; naming laps (`Lap::title`);
  splits in miles.

## Open questions

None.

## Architecture

### The laps (`storage/Laps.h`, `Laps.cpp`), pure

- **`Split { Kind Distance|Time; float32 every; }`** and
  **`parse_split(text)`**: "1 km", "1km", "0.4 km", "10 min", "10min",
  or an error saying what it takes. At least 0 km or 0 min, not 0.
- **`split_starts(activity, split)`**: the times of the records where
  each split starts, after the first: the first record whose distance
  from the start, or whose time since it, reaches the next multiple. An
  error for a distance split without distance, and for one that would
  make more than `MaxLaps` (1,000) laps, such as "0.001 km".
- **`mark_starts(activity, starts, first, last)`**: `starts` with the
  range of records `first..last` made a lap: a start at `first`'s time,
  one at the time of the record after `last` (none if `last` is the last
  record), each moved to a start within `MarkSnapSeconds` (5) of it, and
  the starts in between removed.
- **`laps_at(activity, starts)`**: the `Lap`s the starts make: each from
  the first record at or after its start to the next lap's first record,
  with `range_segment()`'s stats. Starts at or before the first record,
  after the last, or on the same record as another, are passed over: no
  lap is empty.
- **`device_lap_starts(activity)`**: the device's laps' starts, after
  the first, which a mark or a split starts from.

### The activity (`storage/Activity.*`)

- `Activity::device_laps`: the FIT file's laps, kept by `analyze()`.
- `Activity::user_lap_starts`: `std::optional<std::vector<i64>>`, the
  user's laps, from the sidecar: nothing for the device's, an empty list
  for one lap, which a split can leave.
- `lap_starts()`: the user's starts, or the device's.
- `set_lap_starts(std::optional<std::vector<i64>>)`: saves them in the
  sidecar, and only then sets `laps` to `laps_at()`, or back to
  `device_laps` for nothing. An activity without a sidecar path can't,
  as for a rename.
- The sidecar: `read_sidecar_laps()` and `save_sidecar_laps()`, the key
  `"laps"`, through `update_sidecar()`. `load_from_dir()` reads them
  after `analyze()`, which the laps' stats need; starts that aren't
  numbers are passed over, with a warning, and leave the device's laps.

### The app (`SweatTrails.cpp`, `widget/`)

- Tasks `st-mark-lap` (Ctrl+L), `st-split-laps` (Ctrl+Shift+L, an
  `input_box` as for a rename) and `st-device-laps` (Ctrl+Alt+L), on the
  activity on display, as the lap keys are, with what goes wrong on the
  status line.
- `DisplayedActivity::lap_starting_at(record)`, to select the lap a mark
  made; a split and going back select the whole activity.
- `ActivityDisplay::laps_changed()`: the table's rows, its policy and the
  strip from the activity's laps again, as `set_activity()` sets them.

## Test plan

- `LapsTest` (new): `parse_split()`, good and bad; `split_starts()` by
  distance and by time, the last lap what's left, without distance, too
  many laps; `mark_starts()` in one lap, across a start, at the ends of
  the activity, snapping within 5 s; `laps_at()`'s records and stats,
  and starts that would make empty laps.
- `ActivityLoadTest`: a sidecar's `"laps"` replace the device's laps on
  loading, `[]` is one lap, bad ones leave the device's;
  `set_lap_starts()` saves and goes back, keeping the rest of the
  sidecar, and a sidecar that can't be written leaves the laps.
- `DisplayedActivityTest`: `lap_starting_at()`.
- By hand: the three keys, the table and strip after each, and the laps
  after reopening the activity.

## Review notes

*Filled in by the Reviewer.*
