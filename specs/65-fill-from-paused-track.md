# Filling a gap from a Wahoo's paused track

Status: Done

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

A user who paused their Wahoo ELEMNT and rode on without starting it
again is missing that stretch from the activity's distance and moving
time, and so from the totals. The ELEMNT logged where it was while it
was paused (`64-wahoo-paused-track.md`), and the map shows that as a
dashed line. With this feature the user fills the gap with that track,
as `30-fill-gaps.md` fills it from a friend's file, but without another
file: the activity is whole again from its own FIT file.

## Feature

### What there is now

- `30-fill-gaps.md` fills **gaps**: two records in a row more than
  90 s apart, with positions more than 100 m apart. The user drops
  another FIT file on the window, the app finds the gaps that file
  covers, asks "Fill 2 gaps: 1.4 km at 0:23:10, 1.6 km at 0:41:05?",
  and on Yes patches the activity's FIT file, keeping the original as
  `<start>.fit.original`. Ctrl+Shift+F, "Remove the filled gaps", puts
  the original back. The gap gets the other file's position, altitude,
  distance and speed; the records after it, the session's and the lap's
  distance and timer time go up by the gap's; the stop and start events
  around it are left out.
- `64-wahoo-paused-track.md` reads the ELEMNT's paused track (message
  `0xFF00`) into `Activity::paused`, on a deep load of a Wahoo file, and
  draws it dashed on the map. A gap that's been filled has records
  again, so it isn't dashed any more.
- `plan_fills()` (`storage/Gaps.*`) takes the gap's distance from how
  far the other file's distance went up. The paused track's distance is
  frozen at the pause's start, so it can't be used as it is.

### The gap's distance

The gap's distance is the **length of the paused track's path**: the
distances between its positions, one second apart, added up. On the
ride of 2026-11-22, on the three stretches the ELEMNT recorded, its own
distance and the length of its path agree to within 0.3%; adding up
its speed comes to 3% less. So the path is what the device would have
counted had it been running: 6.6 km for the 17.5-minute pause.

The fill's records' distances carry on from the activity's at the stop
by the path's length up to each, as `30-fill-gaps.md`'s carry on by the
other file's.

### What fills the gap

As in `30-fill-gaps.md`: position, altitude and speed from the paused
track, and the distance above. The paused track has no heart rate,
cadence or power, so the gap has none, as a gap filled from a friend's
file has none. Everything else, the totals, the events, the original,
undoing, is as `30-fill-gaps.md` has it.

### Asking for it

- A command, **"Fill the gaps from the paused track"**, in the command
  palette and on **Ctrl+F**, next to Ctrl+Shift+F, "Remove the filled
  gaps". It works on the activity on the main screen, as the drop does.
- It asks first, with `30-fill-gaps.md`'s question, "Fill 2 gaps: 1.4 km
  at 0:23:10, 1.6 km at 0:41:05?", Yes or No, and after a Yes loads the
  activity again and says "Filled the gap: 6.6 km" in the status line.
- It isn't offered by itself when the activity is shown, and nothing is
  filled on a download: a box every time the activity is looked at until
  the user says yes would nag, and a FIT file is only rewritten when the
  user asks. The dashed line on the map is the hint that there's
  something to fill.
- The drop of another file (`30-fill-gaps.md`) stays as it is. Either
  fills a gap; once it's filled, it has records, and neither finds it
  again.

### Which gaps

- Only **gaps** in `30-fill-gaps.md`'s sense: more than 90 s and more
  than 100 m between two records. A pause where the user stood still,
  such as the 2-minute stop of 43 m on the ride of 2026-11-22, isn't
  one: filling it would turn standing time into moving time, and the
  averages with it. It stays a dashed squiggle on the map.
- A gap is filled only if the paused track **covers** it, by
  `30-fill-gaps.md`'s rule: it has positions within 10 s and 200 m of
  the record before the gap and of the one after. On the ride of
  2026-11-22 the paused track is 0 s and 0 m from the stop, and 1 s and
  10 m from the restart. A gap it doesn't cover is left alone.
- All the covered gaps are filled at once, after the one question, as
  with the drop.

### When there's nothing to fill

On the totals screen, or with a box open, the command isn't offered
and Ctrl+F does nothing, as with Ctrl+Shift+F. On the activity, each of
these says so in a message box, and nothing changes:

- On a single leg of a multisport activity: "Not on a single leg: press
  Tab to go to Whole.", as the drop says.
- An activity without a paused track, which is every one that isn't
  from a Wahoo: "This activity has no paused track to fill gaps with."
- No gaps: "This activity has no gaps to fill."
- Gaps, but none the paused track covers: "The paused track doesn't
  cover this activity's gaps."
- A FIT file that can't be patched (`30-fill-gaps.md`): "This activity
  can't be filled: <why>.", as the drop says.

### README

The README's part on filling gaps says that a Wahoo's paused track can
fill them too, with Ctrl+F.

### Version 1 scope

Everything above. Deferred:

- Filling a pause that isn't a gap, such as a short one where the
  device was ridden slowly *(deferred)*: no case for it yet.

## Open questions

None.

## Architecture

### The plan: `storage/Gaps.*`, pure

The paused track is turned into what `plan_fills()` already takes, the
records of another activity, so the covering, the records inserted and
the distances carrying on are `30-fill-gaps.md`'s code, not a second
copy of it:

- **`along_path(paused)`** (in `Gaps.cpp`, not exported) gives the
  paused track's records with their distance replaced by the length of
  the path up to each: the distances between positions with a fix,
  added up. A record without a fix gets the length so far, and so no
  position and a distance in the fill, as a friend's would.
- `plan_fills(records, gaps, along_path(paused))` then does the rest:
  the covering points are the paused points nearest the stop and the
  restart, within 10 s and 200 m; the gap's distance is the path
  between them; the inserted records are the paused points strictly
  between the stop and the restart. The few metres from the record
  before to the first paused point, and from the last to the record
  after, aren't counted, as a friend's file's aren't.
- The path's distance runs on across the whole paused track, from one
  pause to the next, jumping between them; that doesn't matter, as
  `plan_fills()` only takes differences within one gap.
- **`plan_paused_fills(records, paused)`** gives the fills, or the
  message that says why there are none: "This activity has no paused
  track to fill gaps with.", "This activity has no gaps to fill.", "The
  paused track doesn't cover this activity's gaps.". Pure, so the three
  cases are tested.

### The app: `SweatTrails.cpp`

- **`st-fill-from-paused`**, "Fill the gaps from the paused track",
  Ctrl+F, `.when(on_activity_screen)`, as Ctrl+Shift+F. It takes the
  activity with `whole_for_keys()` (nothing with a box open; the leg's
  message), plans, checks with `check_fills()`, and asks.
- The asking and filling after the plan is the drop's: it moves out of
  `cmd_fill_gaps()` into **`ask_to_fill(sweattrails, displayed,
  fills)`**, which both call: `fill_question()`, and on Yes
  `fill_gaps()`, `show_again()` with "Filled the gap: 6.6 km".
- Nothing else changes: the paused track is already there on a deep
  load, which is how the activity on display is loaded, and the map's
  dashed line goes once the gap has records.

## Test plan

- `GapsTest`, `plan_paused_fills()` on `TestRun.h`'s run, which has a
  gap, with a paused track in it as a Wahoo logs one, a point a second:
  - a track that covers the gap gives one fill, whose distance is the
    path's length (a track that zigzags, so that it's longer than the
    straight line, and whose frozen distance would give 0), and whose
    records carry on from the stop's distance by the path up to each;
  - a point without a fix in the track: a record without a position,
    with the distance so far;
  - no paused track; a paused track but no gaps; a track that starts
    too late, and one 500 m off: each its message;
  - a pause that isn't a gap (standing still) isn't filled.
- `GapsTest` with a temp store, as the drop's: the patched file loads
  again with the path's distance added.
- By hand: Ctrl+F on the ride of 2026-11-22 from the Wahoo, the
  question, the map solid where it was dashed, the distance 6.6 km up;
  Ctrl+Shift+F puts it back. The key's wiring and the box need a
  window.

## Review notes

1. **Suggestion** (`src/App/storage/Gaps.cpp`, `plan_fills()`, nothing
   to fix now): `nearest()` picks the paused point nearest in time to the
   restart, which can be a point just after it that belongs to the next
   pause. The gap's distance then includes the jump between the two
   pauses, which is the distance ridden between them, so it is about right
   for a ride on a road. The spec says the jump doesn't matter; it does a
   little in this one case. Not worth code until a real ride shows it.
2. **Suggestion**: `cmd_fill_from_paused()` and `cmd_fill_gaps()` both
   end with `check_fills()` then `ask_to_fill()`. That is two lines
   twice; fine as it is.
3. **Missing test, added**: `test_plan_paused_fills_two_pauses()` in
   `test/GapsTest.cpp`, two gaps and a track of two pauses (path running
   on across the jump): each gap gets its own pause's path length, and the
   second's records carry on from the watch's distance plus the first
   fill. Passes.
4. **Not tested, needs a window**: the Ctrl+F wiring, `.when()`, the
   leg's message and the box; the by-hand check on the ride of 2026-11-22
   is still to do.

No bugs found. `along_path()`, `plan_paused_fills()` and the shared
`ask_to_fill()` match the spec; the messages are the spec's; the README
matches. Full test run: 86 of 86 pass.


### Resolution

1. No change: the gap's own track ends a second or so before the
   restart, nearer than any point of a next pause, and when it doesn't
   the jump is about what was ridden.
2. No change.
3. Kept.
4. Jan tried Ctrl+F on the ride of 2026-11-22 on 2026-11-22: it works.
