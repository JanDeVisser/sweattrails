# One activity, several recordings

Status: In review, on branch feature/31-multiple-recordings

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

The user sometimes records one ride on two devices at once, a Garmin
and a Wahoo say, each with its own power meter, to compare the power
meters. Both recordings come into the store, from Garmin Connect and
from Wahoo, as two activities that mostly overlap. The app should know
that they're one activity recorded twice: count it once, show it once,
and let the user look at either recording, and compare them.

## Feature

### What there is now

- An activity is a FIT file in the store, `<year>/<month>/<start>.fit`,
  named by its start time, with an optional sidecar `<start>.json` for
  what SweatTrails and the downloads know beyond it (`"title"`,
  `"strava"`, `"laps"`).
- The inbox (`Storage::organize_fit_file()`) files a FIT file by its
  start time. One with the same start time as an activity in the store
  is a duplicate, and goes to `inbox/duplicates/`: that's the same file
  coming in twice, from Garmin Connect and Wahoo, say. Two devices
  started a few seconds apart give two start times, and so two
  activities.
- With two recordings of one ride, every part of the app counts it
  twice: the month's list (Ctrl+M), Left and Right, the totals
  (`27-volume-summaries.md`), and the fitness (`29-fitness.md`), where
  its TSS counts double.
- An activity knows its device's `manufacturer`, from the FIT file.

### Three specs

The feature is split in three, each useful on its own:

1. **This spec**: recognising the recordings of one activity, keeping
   them together in the store, and counting and listing the activity
   once. One key splits a group that's wrong.
2. `32-switching-recordings.md` *(to be written)*: on the activity
   screen, which recordings there are, switching between them, and
   choosing another as the main one.
3. `33-comparing-recordings.md` *(to be written)*: comparing the
   recordings: the power of both on the graph, and their averages, NP
   and the difference, per lap too.

### Recordings of one activity

- Two activities are **recordings of one** if they're of the same sport,
  as the totals count them (a treadmill run is a run, a Zwift ride a
  ride: `volume_sport()`; other sports by their FIT sport), and they
  overlap for at least **80%** of the shorter one's elapsed time. A head
  unit started a minute late still matches; a warm-up recorded on its
  own before a race hardly overlaps the race, and doesn't. A group can
  have three recordings or more.
- One of them is the **main recording**: the one that's listed, counted
  in the totals and the fitness, and shown. The others are kept with it.

### Which one is the main one

- A setting ranks the devices, by FIT's manufacturer names:

  ```json
  "recordings": { "prefer": ["garmin", "wahoo_fitness"] }
  ```

  The main recording is the one from the first device in the list that
  any of them is from. If none is, or two are from the same device, it's
  the longest of those, by elapsed time, which covers most of the
  activity. Without the setting, it's the longest.
- Choosing another by hand is in `32-switching-recordings.md`.

### Keeping them together

- The main recording stays where it is, `<year>/<month>/<start>.fit`.
  The others move, with their sidecars, and their `.fit.original` if a
  gap was filled (`30-fill-gaps.md`), into a folder next to it:
  `<year>/<month>/<start>.recordings/`, keeping their own names, their
  start times.
- So everything that lists the store sees only the main recording, and
  counts the activity once without being changed: the month's list
  (Ctrl+M), Left and Right, the summary file, which drops the others as
  their files are gone from where it looked, and with it the totals and
  the fitness.
- The downloads skip an activity the store has (`Storage::has_activity()`,
  by its start time). That looks in the recordings folders too, so a
  recording isn't downloaded again and filed as an activity of its own.
  A recording can start a little before midnight on the last of a
  month, and its main one after it: the folders of the month before and
  after are looked in too.
- A FIT file that comes into the inbox with the start time of a
  recording is a duplicate, as one with an activity's start time is.

### When recordings are recognised

- **When the inbox files an activity**, it's checked against the
  activities that overlap it, and grouped with them if it's a recording
  of the same one. The main one is chosen again: a new recording from
  the preferred device becomes the main one, and the one that was moves
  into the folder.
- **The name goes with the activity**: when another recording becomes
  the main one, the name the user gave the activity (the sidecar's
  `"title"`) moves to the new main one's sidecar, unless it has one of
  its own. Laps the user made and fills stay with the recording they
  were made on: they're in that recording's times. Both recordings
  usually come in within minutes of each other, before the user has
  named either.
- **Once over the whole store**, at the first start with this feature,
  in the background, as the summary file was built: the status line
  says how far it is, and at the end what it grouped, "Grouped 14
  activities recorded more than once". It isn't done again after that.
- After grouping, the summary file is updated, and the activity on
  display is shown again if it moved.

### Seeing and splitting a group

- The summary line under the activity's title says how many recordings
  it has: "42.3 km · 1:25:10 · TSS 87 · 2 recordings".
- **Ctrl+Shift+R** asks "Split the 2 recordings into separate
  activities?", and on Yes moves the others back to where they'd be
  filed, as activities of their own. They're marked in their sidecars,
  and the main one in its, as not to be grouped again, so that the next
  filing doesn't group them back. An activity of one recording says so.
- The main one keeps its title, laps and fills; each recording keeps its
  own sidecar.

### README

The README says that activities recorded on several devices are kept as
one, which one is the main one, the setting, and Ctrl+Shift+R.

### Version 1 scope

Everything above. Deferred to the specs that follow: seeing the
recordings, switching to one, choosing the main one by hand (32), and
comparing them (33). Also deferred:

- Merging recordings, such as one's GPS with the other's power
  *(deferred)*.
- Grouping by hand activities the rule doesn't group *(deferred)*.

## Open questions

None.

## Architecture

### Where the spec leaves a gap

- **Elapsed time** is the session's, and an activity's span is from its
  start to its start plus that.
- **Candidates for a new activity** are the activities starting from a
  day before it to its end: anything that can overlap it. A candidate
  that is the main one of a group brings its recordings along, so the
  new one joins the group.
- **"Separate"** is the sidecar key `"recordings": "separate"`. An
  activity marked so is never a candidate, and a new one that matches
  only marked ones stays on its own.
- **The once over the store** is done month by month, each with the
  months before and after, as a chunked job like the summary file's, and
  a file `recordings-grouped` in the data directory says it's done.
- **The preferred devices** in the settings that FIT doesn't know are an
  error on the status line, as a bad `thresholds` is, and the app starts
  without preferred devices.
- **Splitting** is a job too, as moving files rebuilds the store's
  index, which only jobs may do (`Storage`'s threading note).

### Recordings (`storage/Recordings.*`)

- **`RecordingInfo`**: a FIT file's path, start, elapsed time, sport and
  sub-sport, manufacturer, and whether its sidecar says separate.
  **`recording_info(storage, path)`** reads it with a shallow load, which
  only reads the session.
- **`same_activity(a, b)`**: same `volume_sport()`, or the same FIT sport
  for sports the totals don't count, and overlapping for 80% of the
  shorter's elapsed time. Pure.
- **`main_recording(infos, prefer)`**: the index of the main one, by the
  preferred devices and then the longest. Pure.
- **`recordings_dir(main_fit)`**: `<start>.recordings/` next to it, and
  **`recordings_of(main_fit)`**: the FIT files in it.
- **`group_recordings(storage, infos)`**: chooses the main one, moves
  the others (`.fit`, `.json`, `.fit.original`) into its folder, with a
  former main one's folder's recordings, and moves the user's title to
  the new main one if it has none. Says what moved, so the app can show
  the main one if it was showing a recording that moved.
- **`group_new(storage, start)`**: for the inbox, after it files an
  activity: the candidates that are the same activity, grouped.
- **`group_month(storage, month)`**: for the once over the store: the
  month's activities, with the months around it as candidates, grouped.
- **`split_recordings(storage, main_fit)`**: moves the recordings back
  where the inbox would file them, and marks them all separate.
- **`manufacturer_by_name(name)`** in `fit/FIT.*`, next to `tag()`.

### The store (`storage/Storage.*`)

- `has_activity(start)` also looks in the recordings folders of its
  month and the months before and after it; `organize_fit_file()` uses
  it for duplicates, instead of looking only for its own path.
- `process_inbox()` calls `group_new()` for each activity it files.
- `Storage::prefer`, the preferred devices, set from the settings at
  start, as the thresholds are, before any job runs.
- `Activity::recordings`, how many recordings the activity has: 1, plus
  the FIT files in its folder, counted on a deep load. `activity_summary()`
  adds "· 2 recordings" for more than one.

### The app (`SweatTrails.cpp`)

- `JobType::GroupRecordings`, scheduled at start after the inbox and
  before the summary file's update if `recordings-grouped` isn't there:
  a chunk of months per job, its progress on the status line, and the
  count at the end; then it updates the summary file.
- `JobType::SplitRecordings`, from `st-split-recordings` (Ctrl+Shift+R,
  `.when(on_activity_screen)`; Ctrl+Alt+R off macOS, where Ctrl+Shift+R
  renames), after "Split the 2 recordings into
  separate activities?". When it's done, the activity is shown again and
  the summary file updated.
- A notification after grouping, from the inbox or the once over the
  store, with the main ones of what moved: if the activity on display
  was moved into a folder, its main one is shown.

## Test plan

- `RecordingsTest`, pure: `same_activity()` with a device started a
  minute late (same), a warm-up before a race (not), a run and a ride
  (not), a treadmill run and a run (same); `main_recording()` by the
  preferred devices, by the longest, and for two from one device.
- `RecordingsTest` with a temp store, of FIT files from `TestFit.h`
  with their manufacturers: filing two recordings groups them, the
  preferred one main, whichever came first; a third joins the group;
  `has_activity()` finds a recording, also across a month's end; the
  inbox takes a recording's file again as a duplicate; the month's
  starts list only the main one; the user's title goes to a new main
  one; `.fit.original` and sidecars move along; `split_recordings()`
  puts them back and marks them, and filing a fourth that overlaps
  doesn't group them again; `group_month()` over a store filed before
  this feature.
- `DataFieldsTest`: "· 2 recordings" in the summary line.
- By hand: the first start on Jan's store, the count it reports, a
  compare ride listed once, and Ctrl+Shift+R.

## Review notes

*Filled in by the Reviewer.*
