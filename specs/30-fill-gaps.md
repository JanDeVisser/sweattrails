# Filling a gap from another activity

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

Sometimes the user stops their watch during a run, at a traffic light
for example, and forgets to start it again. Until they notice, the watch
records nothing: on a run Jan did, that happened twice, and about 3 km
went missing. A friend ran that part with him and has it in their own
FIT file. The user fills the gap with that file's track, so that the
activity's route, distance and time are whole again, in the app and in
the totals.

## Feature

### What there is now

- The FIT file is never written: what SweatTrails knows beyond it goes
  in the sidecar, `<start>.json`, under keys of its own (`"title"`,
  `"strava"`, `"laps"`), with `update_sidecar()`, which keeps the rest
  (`26-after-the-fact-laps.md`). There's no code to write a FIT file:
  `fit/FIT.*` only reads them.
- The records come from the FIT file, and `Activity::analyze()` works
  out the activity's stats from them. The session's distance and timer
  time come from the session message.
- The app has no way to open a file from outside the store: the inbox
  is the only way in, and it files what's in it. raylib can tell the app
  that a file was dropped on its window.
- The summary file (`27-volume-summaries.md`) reads a FIT file again
  only when its size or time changes; the sidecar doesn't count.

### Gaps

- A **gap** is where the watch was stopped for a while and moved while
  it was: two records in a row more than **90 seconds** apart, whose
  positions are more than **100 metres** apart. A stop at a traffic
  light moves nowhere; a gap where the watch stood still isn't one to
  fill. Both need positions, so an indoor activity has no gaps.
- The limits are constants in the code, as the moving time's 10 seconds
  is (`RangeMovingGap`): a forgotten restart is minutes and hundreds of
  metres, far past both, so there's little to tune.

### Giving the app the other file

- The user **drops the other FIT file on the window**, from the Finder,
  while the activity is on the main screen. raylib tells the app about
  dropped files, so there's no file dialog to build. The file stays
  where it is: it isn't copied into the store or the inbox.
- The app finds the gaps of the activity on display that the dropped
  file covers. There's no other way to list or fill gaps: until there's
  a file to fill them with, there's nothing to do about them.
- **All the gaps the file covers are filled at once, after a confirm**:
  a box lists them, "Fill 2 gaps: 1.4 km at 0:23:10, 1.6 km at 0:41:05?",
  with the time since the start at which the watch stopped, and Yes or
  No. One drop, one rewrite, and one place to say no if the numbers look
  wrong. There's no picking one gap of several: there's no case where
  only one is wanted.
- Once the file is written, the activity is loaded again, and the
  status line says what was filled: "Filled 2 gaps: 3.0 km".
- A drop is a task, submitted with the file's path, as every action the
  user takes is.

### A drop that can't be used

Each of these says what's wrong in a message box, and nothing changes:

- On the totals screen, or with a box open: "Show the activity to
  fill, and drop the file on it."
- Several files at once: "Drop one file at a time."
- A file that isn't a FIT file, or can't be read: says so.
- An activity without gaps: "This activity has no gaps to fill." One
  whose gaps the file doesn't cover: the message above, about where the
  user was.

### Is the other file from the same place and time?

- The dropped file covers a gap if it has records from the time the
  watch stopped to the time it started again, and its positions at those
  two times are within **200 metres** of the activity's. Watches set
  their clocks from GPS, so the times agree. A file that doesn't cover
  any gap is refused: "This activity wasn't where you were when the
  watch stopped." Without the check, a run across town at the same time
  would fill the gap happily.

### What fills the gap

- **The track only**: position, altitude, distance and speed, which the
  user shared with whoever ran with them. Heart rate, cadence and power
  are the other person's body's, not the user's, so the gap has none:
  the graphs have a break there, and hrTSS (`29-fitness.md`) leaves it
  out.
- The gap's records are the other file's from just after the stop to
  just before the restart. Their distance carries on from the
  activity's at the stop, by as much as the other file's went up from
  that time.
- Every record after the gap goes on by the gap's distance too: the
  watch's distance carried on from where it was stopped.
- **The totals**: the session's total distance and timer (moving) time,
  and those of the lap the watch stopped in, go up by the gap's distance
  and time. Averages, calories and the like stay as the watch had them:
  the app works its own out from the records, and patching them all
  would be guesswork. The elapsed time doesn't change: the gap's time
  was already in it.
- **The watch's stop and start events** around the gap are left out of
  the patched file, so that it reads as if the watch kept running, which
  is the point of filling it: other programs might take the filled part
  for a pause. The original keeps them.
- The user's own laps (`26-after-the-fact-laps.md`) are boundaries in
  time, so they stay where they were.

### Writing the FIT file

The fill is written into the activity's FIT file, so that everything
that reads it gets the whole activity: the data panel, the map, the
laps, the summary file, which reads a FIT file again when its size
changes, and the video overlay, as well as other programs the file is
taken to.

- There's no FIT writer, and a general one is a big job: definitions,
  CRCs, and the device's own messages and fields, which would have to be
  carried over without being understood. So the file is **patched**: a
  copy of it, byte for byte, with the gap's records added as new
  messages of a definition of the app's own, the distance of the
  records after the gap overwritten in place, and the header's data
  size and the CRC worked out again. Everything else is as the watch
  wrote it.
- **The original is kept**: before an activity's first fill, its FIT
  file is copied next to it as `<start>.fit.original`, which the store
  doesn't take for an activity. The original is only written once:
  a second fill starts from the filled file, and the copy stays the
  watch's.
- **A file that can't be patched safely is refused**, with a message
  that says why, and nothing changes. That's a file with compressed
  timestamps, whose times are offsets from the message before, which
  added messages would shift, or one whose records have no distance to
  carry on. A fallback that keeps the fill in the sidecar instead is
  left out *(deferred)*: it would be a second way to do everything, for
  files that may never come up.
- A download of the same activity again, from Garmin Connect or Wahoo,
  is a duplicate by its start time, which the inbox leaves out, so the
  filled file stays.

### Undoing

- **Ctrl+Shift+F**, "Remove the fills", asks "Put back the activity as
  the watch recorded it?", and on Yes puts `<start>.fit.original` back
  in place of the filled file, and loads the activity again. It undoes
  all the fills at once: undoing one of several would mean patching
  backwards, and dropping the file again fills what's wanted. An
  activity that was never filled says so.

### README

The README says how to fill a gap, what's taken from the other file,
that the original is kept, and Ctrl+Shift+F.

### Version 1 scope

Everything above. Deferred:

- Keeping a fill in the sidecar, for files that can't be patched
  *(deferred)*.
- Marking gaps on the map and graphs, without a file to fill them
  *(deferred)*.
- Filling a gap from a route drawn by hand, or a straight line, without
  another file *(deferred)*.

## Open questions

None.

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
