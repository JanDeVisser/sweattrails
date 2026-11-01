# One activity, several recordings

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

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
