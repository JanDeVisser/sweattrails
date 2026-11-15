# Tracks from Strava's export

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

Some activities have no map although Strava has one: their FIT file in
the store is a rewrite by some program that kept the records' heart
rate but dropped the positions. Jan's ride "Tour de La Haye" on
2025-05-14 (`2025/05/1747224577.fit`) is one. Strava's export has the
device's own recording of each of them. After this spec, importing
Strava's export puts that recording in the rewrite's place, as spec
[50](50-stub-activities.md) does for stubs. Spec
[52](52-strava-export-other-recordings.md) deferred this case: "stored
files without positions where Strava's file of the same second has
them … rewrites that lost their positions, to be looked into
separately".

## Feature

### What there is now

- **The Hague ride's file** is 1,053 bytes: maker 255 ("development"),
  product 26731, the program that made spec 50's stubs, no serial
  number. It has 81 records of heart rate only, one every two minutes,
  no positions, and a distance in the last record only. Its session
  says 63.1 km, 2:39 timer.
- **Strava's file of it** (`activities/15447646427.fit.gz` in Jan's
  export) is the Garmin's own (maker 1, product 4315): 3,007 records,
  3,005 with positions, 13 laps, the same start second, distance and
  timer time.
- **Why nothing replaced it.** Spec 50 replaces a stub, a file without
  records (`is_stub()`, `storage/Stubs.cpp`); this one has records.
  Spec 52 then finds Strava's file the same recording (no serial
  number, totals alike), so it isn't added as another recording either.
- **Jan's store** (2026-11-15) has 25 such files, all brought in on
  2026-06-04, from 2025-03-11 to 2025-05-31: maker 255, records without
  positions, distance in the last record only. 24 have a Strava ID with
  a FIT file in the export, every one with positions: 17 from Wahoo, 5
  from Garmin, 2 from Zwift (a virtual map). One of the 24 is another
  recording in a `.recordings` folder (`2025/05/1746555280.recordings/
  1746555260.fit`). The 25th, `2025/05/1747031344.fit`, has no Strava
  ID.
- **Against Strava's file**, all 24 start at the same second, have the
  same distance (within 0.01%), and timer times within 1 s. The elapsed
  time differs in 4 (by up to 22 minutes): the rewrite wrote the timer
  time as the elapsed time. Spec 52's test for the same recording
  (elapsed within 5 s) would miss those.
- The other files without positions in the store are real: trainer
  rides with power, pool swims, treadmill runs. Strava's file of those
  is the same file, or another recording (spec 52), not one with the
  track the stored file lost.

### Replacing a rewrite without a track

- **A stored file lost its track** when, for its Strava ID, the export
  has a FIT file and
  - no record of the stored file has a position, and Strava's file has
    records with positions;
  - the stored file has no serial number (`file_id`'s or a
    `device_info`'s): a program's rewrite, never a device's own
    recording;
  - both start at the same second, and their distances are within
    0.5%.
- **Strava's file replaces it** as spec 50 replaces a stub: filed under
  its start time, the replaced file kept next to it as
  `<start>.fit.stub`, the sidecar (name, notes, laps, Strava title)
  staying with the activity. In a `.recordings` folder, it replaces
  that recording.
- **It's part of the import of Strava's export** (spec 45, 51, 52),
  the step that replaces stubs, so a dropped `activities.csv` does both.
  The status line counts them as it counts the stubs.
- **The closing message has a sentence of its own** when there are any:
  "24 activities got their map back from Strava's files." ("1 activity
  … its map …" for one), kept apart from the stubs' sentence so that it
  says what happened.
- **A second import changes nothing**: the file in the store has
  positions now.
- **Not replaced**: a file without a track whose sidecar has no Strava
  ID, or whose Strava activity has no FIT file in the export (GPX, TCX,
  or none); such as `1747031344.fit`.

### Version 1 scope

In: replacing rewrites without a track from Strava's export, main
recordings and other recordings, the closing message, and the README's
section on activities that are only a summary.

Deferred: the Garmin Connect and Wahoo downloads replacing these, as
they replace stubs (spec 50) *(deferred)*. They'd have to find a file
without a track as they read the store, and fetch an activity they
think they have; the export covers all 24 in Jan's store.

## Open questions

None.

## Architecture

### Which files lost their track: `storage/Stubs.h`

```cpp
// What a FIT file has, as far as replacing it goes. Reads until that's
// known: most files stop at the first record with a position.
struct Recorded {
    bool records { false };
    bool positions { false }; // A record with a position
    bool serial { false };    // file_id's or a device_info's serial number
    // A program's rewrite that kept the records but lost the track.
    [[nodiscard]] bool lost_track() const;
};
std::expected<Recorded, FITError> what_is_recorded(std::string_view fit);

// Whether `original` is the recording the rewrite `rewrite` lost the
// track of: the same start second, distances within SameRecordingDistance.
[[nodiscard]] bool lost_track_of(RecordingInfo const &rewrite, RecordingInfo const &original);

// Replaces the FIT file `stored`, a listed activity's or a recording's in
// a folder, with `fit`, which starts in the same second: `stored` is kept
// as `<stored>.stub`, the sidecar stays. Nothing is overwritten.
Error<StorageError> replace_in_place(fs::path const &stored, std::string_view fit);
```

`replace_stub()` and `replace_in_place()` share the rename, write and
put-back steps.

### The import: `import_originals()`

The step that replaces stubs also looks at the recordings in the
`.recordings` folders. Each stored file with a Strava ID whose line has a
FIT file is read once with `what_is_recorded()`: without records it's a
stub (listed activities only, as in spec 50); `lost_track()` makes it a
candidate. GPX and TCX lines aren't looked at for rewrites, and not
counted. For a candidate, Strava's file is read; it replaces the stored
one if it has positions and `lost_track_of()` holds on the two
`recording_info()`s. `StubImport` gets `tracks`, the number replaced, and
`retracked`, the listed activities' starts (a folder's recording by its
main one's). The progress counts stubs and candidates together.

### The app

- `job_import_strava_export()` sends `StubsReplaced` with `(start,
  start)` for each of `retracked`, so the activity on display is loaded
  again, and updates the summaries. No grouping: nothing new was filed.
- The status line for the step becomes "Replacing activities with
  Strava's files: 3 of 137", as it does both now.
- `ExportImport::message()` adds "24 activities got their map back from
  Strava's files." ("1 activity got its map back from Strava's file.").

## Test plan

- `stubs_test`: `what_is_recorded()` with positions, without, with a
  serial in `file_id`, in a `device_info`, and without records;
  `lost_track_of()` at and over 0.5%, and a second off;
  `replace_in_place()` for a listed file and one in a folder, and that an
  existing `.stub` is an error that changes nothing.
- `strava_export_test`: `import_originals()` replaces a listed rewrite
  and one in a folder; leaves alone a stored file with a serial, with
  positions, a GPX line, Strava's file without positions, and one with
  another start or distance; a second import replaces nothing; the
  message's new sentence, for one and more.
- `TestRecording` (`TestFit.h`) gets a `device_info` serial.
- By hand: the import on a copy of Jan's store with his export: 24
  replaced, the Hague ride with its map.

## Review notes

*Filled in by the Reviewer.*
