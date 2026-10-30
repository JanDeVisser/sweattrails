# After-the-fact laps

Status: Done

## Goal

The user gives an activity laps it didn't get while it was recorded: a
device that doesn't lap by itself gives one lap, the whole activity, and
a lap button forgotten during an interval session leaves the intervals
unmarked. Laps made afterwards show in the lap table, the strip and the
data panel as the device's laps do (`specs/09-lap-display.md`), so the
intervals can be compared side by side.

## Feature

### What there is now

- `Activity::laps` has a `Lap` per FIT lap message, with its records
  (`first_record`, `end_record`) and its `Segment`, whose distance and
  times come from the lap message and the rest from
  `Segment::analyze()`.
- `range_segment()` (`specs/25-range-stats.md`) works out a lap's stats for
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
  assume (`specs/09-lap-display.md`). There are no gaps that aren't laps.
- **They replace the device's laps.** An activity shows one set: the
  user's, if they've made any, else the device's. The table, the strip,
  Up and Down, and the data panel work on them as on the device's.
- The first change starts from the device's laps: their boundaries
  become the user's, and the change is made to those. An activity the
  device didn't lap starts from one lap, the whole activity.
- Each lap's stats come from its records, with `range_segment()`, as a
  dragged range's do: its distance and times from the records, not from
  a lap message, which a lap made afterwards doesn't have. A lap runs up
  to the next lap's first record, as a device's lap runs up to the next
  one's start, so that the laps add up to the activity: a marked lap
  shows one record's step more than the range did.

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

- With a range dragged in the graph (`specs/25-range-stats.md`), a task
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
  (`specs/11-edit-activity-name.md`). Going back to the device's laps
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
  from the start, or whose time since it, reaches the next multiple.
  Distance is measured from the first record that has one, as devices
  often record a few before they have a fix. An error for a distance
  split without distance, and for one that would make more than
  `MaxLaps` (1,000) laps, such as "0.001 km".
- **`mark_starts(activity, starts, first, last)`**: `starts` with the
  range of records `first..last` made a lap: a start at `first`'s time,
  one at the time of the record after `last` (none if `last` is the last
  record), and the starts in between removed. Each is moved to the
  nearest start within `MarkSnapSeconds` (5) of it; the activity's own
  start and end win, so that a range dragged from the start, or to just
  before the end, leaves no lap of a few seconds.
- **`mark_start(activity, starts, first)`**: where the marked lap
  starts, after snapping, which can be before `first`; and
  **`record_at(activity, time)`**: the first record at or after a time.
  The app selects the marked lap with them.
- **`laps_at(activity, starts)`**: the `Lap`s the starts make: each from
  the first record at or after its start to the next lap's first record,
  with `range_segment()`'s stats up to and including that record, so
  that the laps add up to the activity. Starts at or before the first record,
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
- The sidecar: `sidecar_laps()` and `save_sidecar_laps()`, the key
  `"laps"`, through `update_sidecar()`. `load_from_dir()` reads them
  after `analyze()`, which the laps' stats need, and only on a deep
  load, as a month's list has no records; starts that aren't
  numbers are passed over, with a warning, and leave the device's laps.

### The app (`SweatTrails.cpp`, `widget/`)

- Tasks `st-mark-lap` (Ctrl+L), `st-split-laps` (Ctrl+Shift+L, an
  `input_box` as for a rename) and `st-device-laps` (Ctrl+Alt+L), on the
  activity on display, as the lap keys are, with what goes wrong on the
  status line. Ctrl+L takes a finished drag only (`range_stats`). None
  of them changes the laps while a video renders, as the render reads
  them from the same activity.
- `DisplayedActivity::lap_of(record)`, the lap a record is in, to select
  the lap a mark made, the one with the record at `mark_start()`; a
  split and going back select the whole activity.
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
- `DisplayedActivityTest`: `lap_of()`.
- By hand: the three keys, the table and strip after each, and the laps
  after reopening the activity.

## Review notes

1. **Bug: a split's laps don't add up to the split or to the activity.**
   `laps_at()` (`src/App/storage/Laps.cpp:142`) takes each lap's stats
   with `range_segment(first, end - 1)`, so the stretch from a lap's last
   record to the next lap's first record is in neither lap. Split every
   "1 km" at 5 m a second, each lap shows 995 m and 3:19, not 1,000 m and
   3:20, and the laps add up to 2,985 m of the activity's 2,995 m. A
   pause that falls on a boundary goes missing from both laps' elapsed
   time. The device's laps, from their lap messages, add up. Fix: stats
   up to the next lap's first record, `range_segment(first, end)` for all
   but the last lap. That makes a marked lap's stats differ slightly from
   the dragged range's, which the spec says they match: Jan's call.
   Test: `LapsTest` `test_split_laps_add_up` (fails).
2. **Bug: a distance split is refused if the first or the last record has
   no distance.** `split_starts()` (`Laps.cpp:65`) measures from
   `records.front().distance` and checks only `records.back()`. Devices
   often record a few records before they have a fix: such an activity
   gets "This activity has no distance to split by", though it shows a
   distance. Fix: measure from the first record that has a distance, and
   refuse only if no record has one. Test:
   `test_split_by_distance_without_distance_at_the_start` (fails).
3. **Bug: a mark whose start snaps forward selects the wrong lap.**
   `cmd_mark_lap()` (`src/App/SweatTrails.cpp:818`) selects
   `lap_of(first)`, the lap the range's first record is in. When the
   start snaps forward to a start up to 5 s later, that record is in the
   lap *before* the new one. With laps starting at t+104 and a range over
   records 100..199, the laps are 0..104, 104..200, 200..: Ctrl+L
   selects lap 1, the 104 records before the range. Fix: select the lap
   whose first record is the one at or after the marked start (the
   spec's `lap_starting_at()`). For example, `mark_starts()` could also
   return the marked lap's start. Test: `test_mark_snaps_forward`
   (passes). It pins the laps this case makes; the selection is in the
   app and can't be tested.
4. **Bug: snapping at the activity's start makes the short lap it's there
   to prevent.** `mark_starts()` (`Laps.cpp:97-105`) also snaps a range
   that starts at record 0: with a lap starting 3 s in, a range over
   records 0..99 gives a first lap of 3 s, and the marked lap starts at
   3 s. In the same way, a range that ends 1 to 5 s before the end leaves
   a last lap of a few seconds, because the end isn't something to snap
   to. Fix: take the activity's first record and its end as snap targets
   that win. While at it, snap to the *nearest* start within 5 s rather
   than to the first one in the list: with starts at t+95 and t+101, a
   range from t+100 snaps to t+95. Test:
   `test_mark_from_the_start_doesnt_snap_away` (fails).
5. **Bug: `MaxLaps` is off by one.** `split_starts()` (`Laps.cpp:74`)
   refuses only when `total / every > 1000`. A split that comes to
   exactly 1,000 makes 1,000 starts, which is 1,001 laps, where the spec
   says more than 1,000 laps is refused. Fix: count the laps,
   `floor(total / every) + 1 > MaxLaps`. Test:
   `test_split_refused_at_one_more_than_max_laps` (fails).
6. **Bug (minor): Ctrl+L during a drag marks the unfinished range.**
   `cmd_mark_lap()` (`SweatTrails.cpp:809`) checks `segment`, which is
   set while the mouse button is still down. Press Ctrl+L with the button
   held and you get a lap of whatever is dragged so far, even a single
   record, or less than `MinRangeSeconds`, which `end_segment()` would
   have cleared. Fix: require `range_stats`, which only a finished drag
   of 10 s or more has. Can't be tested without the app's command.
7. **Suggestion: only read the laps on a deep load.** `load_from_dir()`
   (`src/App/storage/Activity.cpp:209`) reads and parses the sidecar a
   second time for every activity, including the shallow load of every
   activity in a month's list. Those have no records, and get no laps
   from them. Guard it with `depth == LoadingDepth::Deep`.
8. **Suggestion: lap changes during an overlay render.** The render steps
   on the same `DisplayedActivity`, so Ctrl+L, Ctrl+Shift+L or Ctrl+Alt+L
   during a render changes the lap field and the profile's lap lines
   halfway through the video. `OverlaySource` copies the title for this
   reason. Either refuse to change laps while rendering (status line), or
   snapshot the laps in the source.
9. **Suggestion: naming.** The spec's Architecture names
   `lap_starting_at(record)`, the code `DisplayedActivity::lap_of(t)`.
   `overlay/Fields.cpp` already has a `lap_of(activity, record)`, 1-based,
   as a double. Bring the spec in line with the code, and consider one
   shared function on `Activity`.

The Reviewer added tests, uncommitted: in `test/LapsTest.cpp`, the five
named above and `test_mark_whole_activity` and `test_without_records`,
which pass. In `test/ActivityLoadTest.cpp`,
`test_laps_out_of_order_in_the_sidecar` and `test_device_laps_keep_strava`,
which pass. `laps_test` fails on findings 1, 2, 4 and 5. The rest of the
suite passes, leaving out `oauth_test`.

### Resolution

1. Fixed: a lap's stats run up to and including the next lap's first
   record, so a split's laps add up to the split and to the activity. A
   marked lap's stats can differ from the dragged range's by that one
   record, which the Feature section now says.
2. Fixed: a distance split measures from the first record that has a
   distance, and is refused only if no record has one.
3. Fixed: `mark_start()` says where the marked lap starts after
   snapping, and Ctrl+L selects the lap with the record at that start.
4. Fixed: the activity's start and end win as snap targets, and a mark
   snaps to the nearest start within 5 s, not the first in the list.
5. Fixed: a split is refused when it would make more than `MaxLaps`
   laps, counting the last.
6. Fixed: Ctrl+L takes a finished drag only, one with `range_stats`.
7. Fixed: `load_from_dir()` reads the laps on a deep load only.
8. Fixed: the lap keys change nothing while a video renders, and say so
   on the status line. Snapshotting the laps in the overlay's source
   would let them change, but the video would then not show the laps the
   screen does.
9. Fixed: the Architecture names `lap_of()`, `mark_start()` and
   `record_at()`. `overlay/Fields.cpp`'s `lap_of()` is kept apart: it's
   the lap field's value, 1-based and a double, for the records the
   render steps through, where the app's is an index to select.

The Reviewer's tests are kept, and all pass.
