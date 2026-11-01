# Filling a gap from another activity

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

### Where the spec leaves a gap

- **The gap's time counts as moving**: the session's and the lap's timer
  time, and their moving time if the watch wrote one, go up by the time
  from the stop to the restart. So does the activity message's timer
  time.
- **The lap the watch stopped in** is the lap message whose start time
  is at or before the stop and whose end is at or after the restart. A
  gap that no lap message spans patches no lap.
- **The stop and start events** left out are the timer events (event 0)
  from the stop's time to the restart's, inclusive.
- **The totals screen** is told: after a fill or its undo, the summary
  file is updated, as after the inbox, since the file's size changed.
- **The original** goes along if the store ever moves or removes the
  activity's files; it doesn't now (nothing removes activities).

### The FIT patch (`fit/FITPatch.*`), on bytes, pure

The reader (`fit/FIT.*`) turns messages into values and forgets where
they were, so the patch has a small scanner of its own, which knows
only what it needs: where each message starts and ends, and where its
fields are.

- **`FITMessage`**: its offset and size in the file, whether it's a
  definition, its local type, and, for a data message, its global
  message number and its definition's fields, each with its number,
  offset in the message, size and byte order. **`scan_fit(bytes)`**
  reads the header and the messages, developer fields' sizes included,
  and refuses a file with compressed timestamp headers, a truncated
  one, one whose header doesn't say FIT, or one with more after its
  CRC, which would be a second FIT file chained to it.
- **`FITFill`**: what to put in one gap. The stop's and the restart's
  times (FIT timestamps), the records to insert (timestamp, position in
  semicircles, altitude, distance and speed, each optional), and the
  gap's distance (m) and time (s).
- **`patch_fit(bytes, fills)`** gives the new file's bytes, or why it
  can't:
  - The header and every message are copied as they are, except the
    following.
  - Before the first record message at or after a restart, it inserts a
    definition message of its own, the gap's records, and then the
    definition that had that local type before, copied, so that the
    messages after it read as they did. Local type 15 is used, which
    watches rarely use; any one would do, because of the copy.
  - A record message after a gap gets its distance field (5) raised by
    the gaps' distances before it, in place, in the field's byte order.
    An invalid value (all ones) stays.
  - The session (18) and activity (34) messages' total distance (9),
    timer time (8, activity 0) and moving time (59) are raised by all
    the gaps; a lap's (19: 9, 8, 52) by the gaps it spans.
  - Timer events in a gap are left out.
  - The header's data size is set, its CRC worked out again if it has
    one, and the file's CRC at the end.
  - A file with fills whose records have no distance field is refused.
  - **`fit_crc(bytes)`**, FIT's CRC-16, as the SDK gives it.

### Gaps (`storage/Gaps.*`)

- **`Gap`**: the index of the record before it in `Activity::recorded`,
  the stop's and the restart's times, and the positions there.
  **`find_gaps(records)`**: records in a row more than `GapTime` (90 s)
  and `GapDistance` (100 m) apart, by position. Pure.
- **`distance_between(Coordinates, Coordinates)`** in `Coordinates.*`,
  haversine, in metres: there's none yet.
- **`plan_fills(records, gaps, other_records)`** gives a `FITFill` per
  gap the other activity's records cover: it has a record within
  `CoverTime` (10 s) of the stop and of the restart, with a position and
  a distance, and those positions are within `CoverDistance` (200 m) of
  the activity's. The gap's distance is how far the other activity's
  distance went up between those two records. The inserted
  records' distances go on from the activity's at the stop. Pure.
- **`fill_gaps(storage, activity, fills)`**: copies the FIT file to
  `<start>.fit.original` if there isn't one, patches the FIT file, and
  writes it with `write_file_atomically()`. **`remove_fills(storage,
  activity)`** moves the original back. Both say why if they can't.
- The other file is read as `read_summary()` reads one, an `Activity`
  loaded from its path, deeply, from outside the store; its `recorded`
  are the records as the watch wrote them.

### Dropped files (`src/Lib/Widget/App.*`)

- `App::process_input()` asks raylib whether files were dropped, and
  calls a virtual **`files_dropped(paths)`**, also while a modal is
  open, which the app checks, and which does nothing unless the app
  overrides it. Nothing in it is SweatTrails': it belongs in the
  widget library.

### The app (`SweatTrails.cpp`)

- `files_dropped()` submits **`st-fill-gaps`** with the paths. The task
  checks the drop (`drop_problem()`, pure: one file, on the activity
  screen, no box open), reads
  the file, finds and plans the fills, and asks "Fill 2 gaps: 1.4 km at
  0:23:10, 1.6 km at 0:41:05?" (`fill_question()`, pure). On Yes it
  fills, loads the activity again, sets the status line, and updates
  the summary file.
- **`st-remove-fills`**, Ctrl+Shift+F, `.when(on_activity_screen)`, asks
  and moves the original back, and loads the activity again.
- The work is done on the UI thread: two FIT files of a run read and
  one written take a fraction of a second, and the confirm box is
  there anyway.

## Test plan

- `FITPatchTest`, on files built with `test/TestFit.h` (which gets
  records with positions, distance and a timer event, and a lap):
  `scan_fit()` finds every message and the fields' offsets, including
  developer fields, and refuses compressed timestamps and truncated
  files; `fit_crc()` against a known value; `patch_fit()` gives a file
  the reader (`FITFile`) reads back with the gap's records in order,
  the distances after the gap raised, the session's and the lap's
  totals raised, the timer events in the gap gone, the messages after
  the insert still read with their own definition, and a correct CRC;
  two gaps; a big-endian file; a file without a distance field refused.
- `GapsTest`: `find_gaps()` with a stop that didn't move (no gap), one
  short in time (no gap), and two gaps; `plan_fills()` with a file that
  covers both, one that starts too late, and one 500 m away;
  `distance_between()` against a known distance.
- `GapsTest` with a temp store: `fill_gaps()` writes the original once,
  a second fill keeps it, and `remove_fills()` puts it back; the
  activity loads again with the filled distance.
- `SweatTrails` tests don't exist: the drop's checks and the question's
  text are pure functions, tested in `GapsTest`; the wiring, the drop
  and the box are tried by hand.
- By hand: dropping the friend's file on the run with the two gaps, the
  map and the distance after, Ctrl+Shift+F, and the file opening in
  another program (Garmin's FIT SDK `FitCSVTool`, if at hand).

## Review notes

1. **Bug: with two gaps filled from one drop, the second gap's
   distance goes back by the first gap's distance.**
   `storage/Gaps.cpp:87` and `:108` start each fill's records from the
   watch's distance at that gap's stop, as `Activity::recorded` has it:
   the distance in the file, before any gap is added. `patch_fit()`
   (`fit/FITPatch.cpp:154`) writes those distances as they are. It
   raises only the watch's own records after a gap (`:326`), not the
   records it inserts. So every record of the second gap is short by
   the first gap's distance. On the run in the spec, with two gaps of
   about 1.5 km, the distance graph drops by 1.4 km at the start of the
   second gap and jumps back at its end. Splits and anything else that
   reads record distances are off for that stretch. The totals and the
   watch's own records are right. Filling the gaps one drop at a time
   would work, because the second drop reads the patched file, but one
   drop fills all gaps, and Jan's run has two.
   Failing test: `GapsTest` `test_fill_two_gaps` (597 m at 300 s after
   897 m at 299 s; it expects 900 m). Fix: raise the inserted records'
   distances by the earlier fills' distances, either in `patch_fit()`
   or in `plan_fills()`.

2. **Bug: an activity whose records have no distance gets the wrong
   message.** `storage/Gaps.cpp:87-88`: `plan_fills()` skips a gap when
   the watch's stop record has no distance, so `cmd_fill_gaps`
   (`SweatTrails.cpp:1284`) says "This activity wasn't where you were
   when the watch stopped." The user was right there. The spec says
   such a file is refused "with a message that says why". `patch_fit()`
   has that message ("its records have no distance"), but the user
   never sees it. Failing test: `GapsTest`
   `test_plan_fills_watch_without_distance`. It expects the gap to count
   as covered, so that `patch_fit()` refuses it with its own message. A
   check in `cmd_fill_gaps` would fix the message just as well, and the
   test can be changed to match.

3. **Bug (small): a failed reload after a fill or an undo isn't shown.**
   `SweatTrails.cpp:1253`: `show_again()` puts the load error on the
   status line. Then the caller overwrites it at once with "Filled 2
   gaps: 3.0 km" (`:1303`) or "The fills are removed" (`:1328`). If the
   patched file doesn't load, the user reads that the fill worked while
   the old activity is still on screen. `show_again()` could return
   whether it loaded, and the callers could set their status only if it
   did. Can't be tested without a window.

4. **Suggestion: check whether the file can be patched before asking.**
   `cmd_fill_gaps` asks "Fill 2 gaps: ...?" (`SweatTrails.cpp:1305`)
   and only calls `patch_fit()` after Yes (`:1294`). A file with
   compressed timestamps, or with no record after a gap, gets "Couldn't
   fill the gaps: it can't be filled: it has compressed timestamps"
   only after the user said Yes. Nothing changes, as the spec wants,
   but patching first (in memory, it's pure) and asking only when it
   works would spare the user a question that can't be answered with
   Yes. It would also avoid the doubled wording ("Couldn't fill the
   gaps: it can't be filled: ...").

5. **Suggestion: the gap's time and distance run from record to record,
   not from the watch's stop to its restart.** `plan_fills()` uses the
   last record before the gap and the first after it as the stop and the
   restart (`Gaps.cpp:94-96`). It also takes the other file's record
   nearest to each, up to 10 s away (`nearest()`, `:40`). With 1 s
   recording this makes no difference. A watch on "smart recording"
   writes records several seconds apart. Then the time from the last
   record to the button press is counted twice: by the watch's timer
   and again in the gap's timer time (`patch_fit()` adds `restart -
   stop`). The distance is counted twice in the same way, by up to
   about 10 s of running at each end. The timer events in the file give
   the real stop and restart times, and interpolating the other file's
   distance at those times would make the fill exact. This isn't wrong
   by the spec's words, so no test.

6. **Suggestion: messages in the gap are left ahead of the inserted
   records.** `patch_fit()` inserts the gap's records in front of the
   first record at or after the restart (`FITPatch.cpp:313-321`). So a
   non-timer message the watch wrote during the gap, such as a battery
   event at 200 s, comes before inserted records from 100 s to 399 s.
   The message is kept, as it should be (`test_patch_keeps_other_events`),
   but the file is no longer in time order. Readers that assume time
   order, like other programs the file goes to, may misplace it. Watches
   write little while stopped, so this is minor.

7. **Suggestion (style): `std::filesystem::exists()` that can throw.**
   `SweatTrails.cpp:1315` calls the overload without an error code.
   That throws if the directory can't be read, and the app uses no
   exceptions (`STYLE.md`). `remove_fills()` already does the same
   check with an `std::error_code`: let it give "This activity has no
   fills to remove." before the question, or pass an error code here.

Checked, and found right: `scan_fit()` sizes for definitions with
developer fields, big-endian definitions, local types redefined
mid-file, and chained files. The definition of local type 15 is put
back after the insert. A 0xFFFFFFFF record distance stays invalid.
Session, activity and lap totals, and only the spanning lap raised.
Only timer events in the gap dropped. The header data size, the header
CRC (worked out again only if the watch wrote one), and the file's CRC,
written anew rather than copied, with or without a trailing CRC in the
input. The u32 sums can't overflow for real activities. `find_gaps()`
and `plan_fills()` use strict "more than" for 90 s and 100 m, and
"within" for 10 s and 200 m. Records in the other file without a
position or a distance go in with invalid values and read back without
them. The original is written once. If writing the patched file fails
after the original was kept, the FIT file is unchanged and the original
equals it, so an undo is harmless. `cmd_fill_gaps` captures the
displayed activity's `shared_ptr` when the file is dropped. On Yes it
fills that activity, even if the user moved to another while the box
was open, and shows it again. `App::files_dropped` copies the paths
before `UnloadDroppedFiles()`, and runs while a modal is open, as the
spec says.

**Tests added** (uncommitted, as asked):
- `test/TestRun.h`: `RunOptions::other_events` and `two_laps`,
  `event_fields()`, `lap_fields()`, `run_two_gaps()` (a run with two
  gaps whose distance carries on from each stop, as a watch's does),
  and `with_crcs()` (a header CRC and a trailing file CRC).
- `test/FITPatchTest.cpp`: `test_patch_with_crcs`,
  `test_patch_keeps_other_events`,
  `test_patch_only_the_lap_with_the_gap` and
  `test_patch_keeps_an_invalid_distance`. All pass.
- `test/GapsTest.cpp`: `test_fill_two_gaps` (**fails**, finding 1),
  `test_plan_fills_watch_without_distance` (**fails**, finding 2), and
  `test_fill_with_records_missing_values` (passes).

`ctest --test-dir build -E oauth`: 50 of 51 pass. `gaps_test` fails,
only on the checks of findings 1 and 2.
