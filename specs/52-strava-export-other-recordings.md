# Importing the other recordings in Strava's export

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

Spec [51](51-strava-export-activities.md) imports the export's files
for the Strava IDs the store doesn't have, and means to keep "Zwift's
file next to the head unit's" as another recording (spec
[31](31-multiple-recordings.md)). It misses the common case, though:
the titles run (`match_strava()`, `StravaMatchSeconds`) already gave
Strava's ID to the head unit's recording, so `queue_missing()` takes
the ID as imported and skips Strava's file, which is a different
recording.

Example: `2026/10/1791310990.fit` is a Zwift ride recorded by a Garmin
(manufacturer 1, `indoor_cycling`, no positions). Strava's activity
20480258698 for it is Zwift's own file (manufacturer 260, positions in
Innsbruck), starting 16 s earlier. The Garmin file has the Strava ID,
so Zwift's file was never imported, and the ride has no map.

After this spec, Strava's file for an ID the store already has is
imported as another recording when it really is a different one.

## Feature

### Jan's export against the store

Jan's export (2026-11-11) against the store (2026-11-14). These are the
5768 stored files, main or recording, whose sidecar has a Strava ID,
compared with the export's file for that ID (FIT only):

| Export's file against the stored one     | Count |
|------------------------------------------|------:|
| Byte for byte the same                   |  3081 |
| Different bytes, same start second       |  2276 |
| Different bytes, start 1 to 60 s off     |   226 |
| Different bytes, start more than 60 s off |   65 |

Same start second is the same recording rewritten: by Garmin Connect,
or by SweatTrails filling gaps (manufacturer 255).

The 226 that are 1 to 60 s off break down like this:

| What                                                        | Count |
|-------------------------------------------------------------|------:|
| Already in the store as another recording of the group     |    45 |
| Not in the store, same totals (elapsed ±5 s, distance ±0.5%) |  106 |
| Not in the store, different totals                          |    75 |

- *Already in the store*: the group has a recording within 60 s of the
  export file's start, with the same elapsed time. The ID landed on the
  "wrong" recording; nothing to import.
- *Same totals*: mostly the same recording, rewritten with a few
  seconds' shift (e.g. 71 Zwift rides stored as Garmin Connect's
  manufacturer 255 copy, and 2015 rides of manufacturer 54). Importing
  them would add a bogus second recording.
- *Different totals*: the real other recordings, 49 of them with
  positions only in the export's file (Zwift next to a Garmin or Wahoo
  head unit, like the example). Some stored files are fragments
  (a 2 s run against Strava's 20 min one).

By overlap (spec 31's measure: the time both cover, as a share of
the shorter one's elapsed time), and by the devices' serial numbers
(`file_id` and `device_info`):

| Start      | Overlap of the shorter | Serial numbers | Count |
|------------|------------------------|----------------|------:|
| 1 to 60 s  | 80% or more            | different      |    97 |
| 1 to 60 s  | 80% or more            | none in one    |   125 |
| 1 to 60 s  | less than 20%          | (fragments)    |     4 |
| > 60 s     | less than 20%          | any            |    62 |
| > 60 s     | 20% or more            | none in one    |     2 |

- Measured on the shorter recording, a recording started late or
  stopped late still overlaps fully: 6 pairs overlap 80% of the shorter
  but less than 80% of the longer, such as 322 s of a Wahoo head unit
  inside a 2 h Zwift ride.
- Overlap doesn't tell a rewrite from another device: the rewrites
  overlap the stored file almost completely too. Different serial
  numbers do (97), but rewritten files (manufacturer 255) have none
  (125).
- The 62 that start more than 60 s off don't overlap the stored file:
  the Strava ID is on the wrong activity, and Strava's file is a
  separate activity that's likely missing from the store.

### The import

This changes `queue_missing()`'s step of the `ImportStravaExport` job
(spec 51). Lines whose Strava ID no activity in the store has are
handled as before. For a line whose ID the store *has*, on a main
recording or in a `.recordings` folder, the import now compares
Strava's file (ungzipped) with the stored file that has the ID:

1. **The same bytes**: nothing to do, as now.
2. **The same recording**: nothing to do. Two files are the same
   recording when
   - they share a device serial number (`file_id`'s, or a
     `device_info`'s), or
   - one has no serial number (Garmin Connect's and SweatTrails' own
     rewrites have none), and their totals match: elapsed time within
     5 s, distance within 0.5%.
   Otherwise they're different recordings.
3. **A different recording that overlaps**: Strava's file overlaps the
   stored one by spec 31's rule (the same sport as the totals count
   them, and at least 80% of the shorter one's elapsed time). It goes
   to the inbox as `strava-<id>.fit` with its sidecar, as in spec 51,
   unless a recording of the stored file's group is already the same
   recording as Strava's file (by test 2): then the ID only landed on
   the other recording, and there's nothing to do. The inbox files it
   and groups it with the activity (spec 31); a start 1 to 60 s off
   isn't set aside as a duplicate. Spec 31's 80% of the *shorter* one
   is kept: a parallel recording started or stopped late still overlaps
   fully, as the data above shows, and a lower threshold would change
   grouping for every import.
4. **A different recording that doesn't overlap**: the Strava ID is on
   the wrong activity, and Strava's file is a separate one. The import
   removes the `"strava"` title from the stored file's sidecar (only
   that key: the user's own title and notes stay), and sends Strava's
   file to the inbox, which files it by its start time, or sets it
   aside if the store has it already. The removed title isn't the
   user's own name: the titles run gave it.

The counts in the closing message include these: what the inbox adds or
sets aside is counted as in spec 51, and a sentence counts the moved
IDs when there are any: "Moved 64 Strava titles to the right
activity."

A second import doesn't queue the files again: after the first, the
group has a recording that's the same as Strava's file (test 2), or
the inbox set it aside (`set_aside_as_duplicates("strava")`, as in
spec 51).

### The map of a group

Spec 31 keeps the preferred device's recording as the main one
(`"prefer": ["garmin", …]`), so a Garmin indoor ride stays the main one
after Zwift's file joins it, and still has no positions.

- When the recording shown (main or another, spec 32) has no
  positions, the map shows the route of a recording of the same
  activity that has: the longest of those, by elapsed time.
- The hover mark (the graph's cursor on the map) is placed by time of
  day: at the nearest record of the route's recording.
- The map says whose route it is, as a caption in its corner:
  "Route: Zwift", named as spec 32 names devices.
- The main recording, the totals and the data panel don't change.

### Version 1 scope

In: Strava's files for IDs the store has, as other recordings or as
separate activities, by the tests above; moving a Strava title off the
wrong activity; the map's route from another recording; the closing
message; the README's section on Strava's export.

Deferred:

- Stored files without positions where Strava's file of the same
  second has them (29 in Jan's store, mostly a manufacturer 255
  rewrite against Wahoo's file) *(deferred)*: rewrites that lost their
  positions, to be looked into separately.
- Fragments (4: stored files of 2 to 4 s against Strava's full
  activity) are handled by the tests as they fall: they don't overlap
  80% of the shorter, so they count as test 4.

## Open questions

## Architecture

### Same recording (`storage/Recordings.*`)

- `RecordingInfo` gets two more facts, both from a shallow load:
  `serial` (`file_id`'s serial number, 0 without one) and `distance`
  (the session's, in m).
- `recording_info()` gets an overload for a FIT file in memory,
  `recording_info(storage, std::string_view fit)`, for the export's
  ungzipped files; the path one fills `fit` as now.
- `same_recording(a, b)`: the same start second (added after the trial
  run below: rewrites keep the start but count pauses otherwise, so
  their elapsed time can differ by minutes; and the inbox can't file
  two files at one second anyway); else the same serial number when
  both have one; else elapsed within 5 s and distance within 0.5%
  (`SameRecordingSeconds`, `SameRecordingDistance`). Two serials that
  differ are different recordings, whatever the totals.
- Only `file_id`'s serial, not the `device_info` listings' as the spec
  says: two head units paired with one power meter both list that
  meter's serial, which would make them one recording. `device_info` is
  only read on a deep load anyway.

### The import (`storage/StravaExport.*`)

- `strava_ids()` becomes `strava_fits()`: for each Strava ID, the FIT
  files in the store with it, main or recording.
- `classify_export_file(export_info, stored)` is the decision, pure, on
  `RecordingInfo`s, so that it's tested without files:
  `enum class ExportFile { Same, OtherRecording, OtherActivity }`.
  `stored` holds, for each stored FIT file with the ID, its info and
  those of its group (main and `.recordings`). Same if any of the group
  is `same_recording()`; OtherRecording if a file with the ID is
  `same_activity()` with it (spec 31); else OtherActivity.
- `queue_missing()` handles the IDs the store has along with the
  missing ones: reads Strava's file, skips it if it's byte for byte a
  stored one (cheap, and 3081 of Jan's are), else loads the infos and
  classifies. Same: nothing. OtherRecording: `to_inbox()` as now.
  OtherActivity: `to_inbox()`, then, once it's there, removes the
  `"strava"` key from the sidecars with the ID (`reject_sidecar_strava()`
  in `storage/Activity.*`, next to `save_sidecar_strava()`).
- Added after the trial run: when the store has a FIT file at Strava's
  file's own start (`Storage::fit_at()`, which `has_activity()` now
  uses), Strava's file is there already, without the ID, and isn't
  queued: the inbox would set it aside. For OtherActivity, the title
  moves to that file's sidecar (`give_title()`), unless it has a Strava
  title of its own. Multisport files are the common case: SweatTrails
  files them by their last session's start. A stub there isn't
  "there": the inbox replaces it.
- `MissingImport` gets `titles_moved`; `message()` adds "Moved 64
  Strava titles to the right activity." The progress callback counts
  over all lines it checks, so the status line moves while the store's
  files are compared.
- Loading 2 × ~2600 files shallowly on the job thread takes some
  seconds; reading every FIT file is what the stub step does too.

### Trial run on a copy of Jan's store

With a scratch driver (not committed) running `queue_missing()` and the
inbox, as the job does, on a copy of the store under another `HOME`,
with Jan's export of 2026-11-11, in 74 s:

| Version                         | Queued | Filed | Set aside | Titles moved |
|---------------------------------|-------:|------:|----------:|-------------:|
| First                           |    227 |    64 |       161 |           58 |
| Same second is the same         |    121 |    64 |        55 |           57 |
| And not what's here at its start |     71 |    64 |         5 |            9 |

63 of the 64 filed were grouped as another recording. The 5 set aside
are one file Strava has 5 uploads of. `2026/10/1791310990` became a group
with Zwift's file as the main one (Jan's settings prefer Zwift).

### The map (`widget/DisplayedActivity.*`, `widget/ActivityMap.cpp`, `SweatTrails.cpp`)

- `DisplayedActivity` gets `std::optional<RouteRecording> route`: the
  other recording's `Activity`, and `pairs`, the shown records paired
  with its records by `pair_records()` (spec 33). The mark and the
  selected range go through `pairs` to the route's records:
  `route_index(size_t shown)`.
- `route_source(std::span<Activity const> recordings)`, pure: the index
  of the longest one with a `segment.box`, or none.
- `SweatTrails::show_activity()` and `show_recording()` look for a route
  when the shown recording has no box and the activity has more than one
  recording: they load the others with `load_to_show()`, as
  `compare_if_on()` does on the UI thread, and set the route before the
  display shows it.
- `ActivityMap::set_activity()` takes the box, the track and the Zwift
  world from the route's activity when there is one, and `draw()` puts
  the caption "Route: <device_name()>" in the top left corner, in the
  attribution's style.

### Strava titles run

- After the import takes an ID off the wrong activity, the titles run
  (`match_strava()`) would give it back: it matches each stored start
  to the nearest Strava activity within 60 s, whatever other activity
  has its ID, and Strava's start is what put the ID there.
- So the import remembers it: the sidecar gets `"strava_rejected"`, a
  list of Strava IDs, next to the removed `"strava"`
  (`reject_sidecar_strava()` does both). The titles run's `save_title()`
  doesn't give an activity an ID its sidecar rejects. Jan chose this over
  skipping IDs another activity has, which fails when the inbox set
  Strava's file aside.

## Test plan

- `RecordingsTest`: `same_recording()` by serial, by totals at and over
  the limits, differing serials with equal totals; `recording_info()`
  from memory reads the serial and distance.
- `StravaExportTest`: `classify_export_file()` for each outcome, also
  with the same recording elsewhere in the group. `queue_missing()` on a
  store with: an ID whose stored file is byte for byte Strava's; a
  rewrite (no serial, same totals); another recording (other serial,
  overlapping); a wrong activity (no overlap), whose sidecar loses its
  `"strava"` but keeps `"title"` and `"notes"`. Through the inbox, the
  other recording is grouped and the other activity filed; a second
  import queues nothing. The message's new sentence.
- `TestFit.h` gets the serial and positions in its builders.
- `DisplayedActivityTest`: `route_source()`, and `route_index()` for
  the mark and a range, including a shown record before the route
  starts.
- Not automated: drawing the route and the caption; checked by hand on
  `2026/10/1791310990`, after importing Jan's export.

## Review notes

1. **Bug: a second import isn't idempotent when Strava's file is here and has a title of its own.**
   `queue_missing()`, the `fit_at()` branch (StravaExport.cpp, "Strava's file is in the store already"): for OtherActivity, `reject()` takes the ID off the wrong activity, and `give_title()` returns early when the file at Strava's start already has a Strava title. The ID is then on no file, so the next import finds `stored.find(id) == end`, queues the file, and the inbox sets it aside (queued 1, set aside 1, a "was already here" sentence), once more. Test: `test_strava_file_already_here()` in StravaExportTest.cpp now runs the import twice and fails at `again.queued` (is 1, expected 0). The spec promises a second import queues nothing. Fix: in that branch don't queue lines whose file is here (for instance add the ID to the set-aside list, or leave the ID on the stored file instead of rejecting it when nobody can take it).
2. **Suggestion: stored files that can't be read count as "no stored file".** `stored_with_id()` leaves out files `recording_info()` can't read, with a warning. If all of a line's files are left out, `classify_export_file()` gets an empty list and returns OtherActivity: Strava's file is queued and the ID is rejected from the unreadable file. Probably harmless for a corrupt file, but a transient read error moves a title for no reason. Skipping the line when `stored` comes back empty is safer.
3. **Suggestion: `route_for()` loads every other recording in full on the UI thread** each time an activity without positions and with several recordings is shown, and on every Tab (`show_recording()`), and `compare_if_on()` loads them again. Fine for Jan's rides; a cache of the route's `Activity` by id would avoid the repeat.
4. **Suggestion: the spec's Architecture says the route's records are paired by `pair_records()`; the code uses `nearest_records()`** (nearest in time, which suits the mark better). Update the spec text.
5. **Suggestion: `titles_moved` is counted when the file was queued, also when `reject()` failed to write the sidecar** (the warning is logged, the count says moved). Minor.

Checked and found right: `same_recording()`'s unsigned differences, `route_index()` at both ends and with an empty shown list (nearest is empty, so the last route record), the selected range's `min`/`max` mapped before the `points.size()` check, comparing mode (the compared recording is separate from the route), recordings of one (no route), stubs (the stub branch returns before the route), the Zwift world taken from the route, and the titles run's `strava_rejected`.

Tests added: `test_nearest_records()` (DisplayedActivityTest.cpp), `test_fit_at()` (GarminTest.cpp), and the second import in `test_strava_file_already_here()` (fails, finding 1).
