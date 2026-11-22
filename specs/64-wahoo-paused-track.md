# The track a Wahoo recorded while paused

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

When a Wahoo ELEMNT is paused, it doesn't record `record` messages, but
it keeps logging where it is, once a second, in a message of Wahoo's
own. A user who paused it and forgot to start it again sees a straight
line on the map across the stretch they rode, although the file knows
the way they went. With this feature the map shows that stretch as it
was ridden, drawn dashed, so it's clear the device was paused there.

## Feature

### What there is now

- The map (`widget/ActivityMap.cpp`) draws the track from the records:
  in red, with the selected range or lap in blue over it, and the
  marked record as a black dot. Across a pause it draws a straight line
  from the last record before it to the first one after.
- The FIT reader (`fit/FIT.*`) knows the profile's messages. Message
  65280 isn't in it, and Wahoo doesn't document it.
- `30-fill-gaps.md` fills a **gap** (more than 90 s and 100 m between
  two records) from another activity's FIT file, which the user drops
  on the window. It patches the FIT file and keeps the original. It
  draws nothing for a gap it hasn't filled: marking gaps on the map is
  deferred there.

### Wahoo's paused track

What's known of message 65280, from an ELEMNT ROAM file (2026-11-22, a
ride with two pauses):

- It's written once a second, only while the device is paused, from the
  pause's timer stop event to its start event, with one more at the
  end of the activity whose fields are all invalid.
- Field 253 is the timestamp, 0 and 1 the latitude and longitude in
  semicircles, 2 the altitude (scale 5, offset 500, as `record`'s), and
  6 the speed in mm/s. Field 5 is the distance, frozen at the pause's
  start. Fields 13, 31 and 33 are unknown (31 may be the GPS accuracy).
- No power, heart rate or cadence.

In that ride, a pause of 17.5 minutes has 1,051 of these messages, about
6.6 km of track, which ends 10 m from the first record after it. The
other, a stop of 2 minutes, has 43 m.

### On the map

- The paused track is drawn as a **dashed line**, in the track's red,
  in place of the straight line across the pause: from the last record
  before the pause, through the paused track's positions, to the first
  record after it.
- **Every pause** with a paused track is drawn, not only gaps in
  `30-fill-gaps.md`'s sense (more than 90 s and 100 m). A short stop
  where the user stood still is a small dashed squiggle, which is where
  they were; filtering it out would hide nothing worth hiding.
- Only the times without records are drawn dashed: a paused-track
  message from before the first record or after the last one (the
  ELEMNT writes one at the end) isn't drawn, and neither is one whose
  position is invalid. That also means a gap that's been filled, whose
  time now has records, has no dashed line.
- It's drawn only for a file whose `file_id` manufacturer is Wahoo: a
  message number that's Wahoo's own may mean something else in another
  maker's file. As far as is known, Garmin devices record nothing while
  paused, so Wahoo is the only case.
- **It's drawn, and nothing else.** It isn't in the records, so the
  graphs, the data panel, the distance and times, the selection and
  laps, and the summaries don't see it. A selected range across the
  pause is blue on the records only. Hovering over it shows nothing, and
  the marked record's black dot is never on it, since there are no
  records there.

### Version 1 scope

The dashed paused track on the map. Deferred:

- Filling a gap from the paused track, as `30-fill-gaps.md` fills it
  from another file *(deferred)*: a spec of its own, which can reuse
  `plan_fills()` and `fill_gaps()`. The dashed line is then the hint
  that there's something to fill.
- The paused track on the video overlay's map (`16-overlay-map.md`)
  *(deferred)*: it should be drawn there too, at some point.

## Open questions

None.

## Architecture

### Reading the paused track

- Message 65280 is `0xFF00`, the first of the manufacturer-specific
  message numbers, which the reader already knows as `mesg_num::vendor`.
  Its fields are numbered as `record`'s are: 13 is the temperature and
  33 the calories, which in the ride above ends at the session's 899
  kcal. So it's read with `record`'s decoder: in `fit/Profile.*`,
  `make_paused_record(rec)` reads it with a copy of `record_meta` whose
  message number is `vendor`, and so with `on_load`, which drops half a
  position. It gives a `record`.
- `Activity` gets `std::vector<record> paused { }`: the paused track, on
  a deep load of a file whose `file_id` manufacturer is
  `wahoo_fitness`. The `file_id` message comes first in a FIT file, so
  the manufacturer is known when the vendor messages are read.

### Which parts are drawn: `storage/Paused.*`, pure

- `struct PausedTrack { size_t before; std::vector<Coordinates> positions; }`:
  the positions of the paused track between `records[before]` and
  `records[before + 1]`.
- `paused_tracks(records, paused)` gives them, for every two records in
  a row more than `RangeMovingGap` (10 s) apart, which is where the app
  already takes a range to have paused, with the paused track's
  positions that have a fix and fall strictly between their times.
  Nothing before the first record or after the last. A gap that's been
  filled (`30-fill-gaps.md`) has records again, at most seconds apart,
  so it has none: the ELEMNT's own track is a second apart, and a
  friend's records that are further apart are drawn solid, as moving,
  as the range stats already count them.

### Dashed lines: `src/Lib/Widget/Dashes.*`

raylib has no dashed line, and the paused track is many short segments,
a second apart, a few pixels each: dashing each segment on its own would
look solid. So the dashes run along the whole polyline:

- `dashes(points, dash, gap)` gives the dashes' ends, as pairs of
  points, carrying the pattern on from one segment to the next. Pure,
  so it's tested.
- `draw_dashed_line(points, width, dash, gap, color)` draws them with
  `DrawLineEx`.

They're in the widget library, as nothing about them is the map's.

### The map

- `ActivityMap::set_activity()` works out the paused tracks of the route
  (the activity whose records it draws) and keeps each as world points,
  with the track's points of the record before and the one after at its
  ends.
- `draw_track()` draws them dashed, in red, under the selection, and
  leaves out the straight line from `before` to `before + 1`, in red and
  in blue alike. The record points, the selection, and the mark are as
  they were.
- Only the activity's map: the video overlay's map
  (`16-overlay-map.md`) is left as it is for now. It needs the paused
  track too, at some point: see Version 1 scope.

## Test plan

- `FITTest` or `ActivityLoadTest`: a FIT file of `TestRun.h`'s run, with
  a paused track in vendor messages in its gap and an invalid one after
  the end, loads it into `paused` when the manufacturer is Wahoo (32),
  with positions, altitude and speed, and the invalid one without a
  position; the same file made by another manufacturer loads none; a
  shallow load loads none; the records are as they were.
- `PausedTest` (new): `paused_tracks()` for a pause with a track; two
  pauses; points before the first and after the last record; points
  without a fix; a pause of 10 s or less; a pause without paused
  points; a filled gap, records a second apart over the paused track,
  gives none.
- `DashesTest` (new): `dashes()` on a straight line; the pattern
  carrying on across short segments; a dash cut at a corner; the last
  dash cut short at the end; fewer than two points; zero-length
  segments.
- The drawing itself, `draw_dashed_line()` and the map, needs raylib
  and a window, so it's checked by looking: the ride of 2026-11-22 from
  the Wahoo, with its 6.6 km pause, in the app. Jan looked on
  2026-11-22: the paused stretch is dashed and joins the track at both
  ends.

## Review notes

*Filled in by the Reviewer.*
