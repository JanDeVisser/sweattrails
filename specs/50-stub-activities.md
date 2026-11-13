# Activities that are only a summary

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

Some activities in the store have no map, no graphs and no laps, even
though they were recorded with a GPS device and the full recording exists
elsewhere. Jan's run on the Bermuda Railway Trail on 2025-02-27
(`2025/02/1740662259.fit`) is one: he recorded it with his ELEMNT RIVAL,
and Strava has the map. The user wants these activities to have their
real recordings, and wants the downloads to stop treating them as done.

## Feature

### What there is now

- **The stubs.** `1740662259.fit` is 425 bytes. It has a file ID, a
  device info, the developer field descriptions Stryd adds (Form Power,
  Leg Spring Stiffness, Air Power), one session with the totals (22.25 km,
  2:00:14 moving) and an activity message. It has **no record messages**.
  Its maker is 255 ("development") and its product 26731: it was made by
  a program from a summary, not written by the RIVAL (Wahoo is maker 32,
  the RIVAL product 33).
- **There are 191 such files** under 1000 bytes in Jan's store, from 2009
  to 2025, all brought in on 2026-06-04 and 2026-06-10, before SweatTrails'
  repository existed (from the old app's store, or its imports). 87 of
  their sidecars also have a Garmin Connect `activityId`.
- **Strava's export has the original of 140 of them.** Jan's export
  (`activities.csv` with `activities/<id>.fit.gz`, spec
  [45](45-strava-export-import.md)) has a file for 140 of the stubs,
  found by the Strava ID in their sidecars. For the Bermuda run that's
  `activities/14666148179.fit.gz`: 355 kB of FIT from the RIVAL (maker 32,
  product 33), created at the same second as the stub. 35 stubs are manual
  entries on Strava too, without a file; 12 have no Strava ID.
- **Why the downloads didn't fix it.** Wahoo's download, including
  Everything (spec 46), and Garmin's skip a workout whose start time
  `Storage::has_activity()` already finds (`Wahoo.cpp`, `Garmin.cpp`),
  and the inbox sets aside a file whose start time is taken
  (`Storage.cpp`, `inbox/duplicates`). The stub holds the slot, so the
  RIVAL's file is never fetched.
- Spec 45 imports descriptions from a dropped `activities.csv` and
  deferred "the original files". zlib is already linked (`Zip.cpp`), so
  reading a `.fit.gz` is cheap.

### Stubs

- A **stub** is an activity whose FIT file has **no record messages**.
  That's what makes it useless on the activity screen, whatever program
  wrote it, so the test is on content, not on size or maker. A real
  recording always has records, indoor ones included.


### Replacing a stub

Both ways in below end in the same step:

- **The original takes the stub's place.** It's filed under its own
  start time, `<start>.fit`, as everything in the store is. Usually
  that's the stub's to the second; when it's a few seconds off, the
  stub's sidecar moves with it (its `"start_time"` updated), so that a
  later download looks for the start time the file really has.
- **The stub is kept** next to it as `<start>.fit.stub`, as spec 30 keeps
  `<start>.fit.original`, which the store doesn't take for an activity.
  Nothing puts it back: it holds nothing the original lacks.
- **The sidecar stays**: the Strava title, Garmin's ID, and the user's own
  title, notes and laps belong to the activity, not to the file.
- The summary file sees the new size and reads the file again (spec 27),
  and the totals screen is told, as after the inbox.

### From Strava's export

- When the user drops `activities.csv` (spec 45), the import also looks
  at every stub with a Strava ID. If the export's line for that ID has a
  `Filename` ending in `.fit.gz` or `.fit`, and the file is there next
  to the CSV, the stub is replaced with it. All 140 of Jan's are
  `.fit.gz`. A `.gpx.gz` or `.tcx.gz` original is skipped *(deferred)*,
  since SweatTrails reads only FIT, and the closing message counts those.
- Matching is by Strava ID, so start times don't matter here.
- The closing message of the import adds a sentence: "Replaced 140
  activities that were only a summary with their recordings." Only when
  there were any; with skipped ones: "3 more have a GPX or TCX file,
  which SweatTrails can't read."
- It's part of the import's job, and its status line counts the stubs
  after the descriptions.
- This is the main way in: 140 of Jan's 191 at once, offline, and the
  only one for the years before Wahoo.

### From the downloads

- **A stub doesn't count as having the activity.** Wahoo's download,
  Everything included, and Garmin Connect's ask for a workout whose start
  time only a stub has, and the inbox replaces the stub with what comes
  in instead of setting it aside as a duplicate. This catches the RIVAL
  activities that Strava's export doesn't, and keeps working for whatever
  comes in later.
- **Start times within `StravaMatchSeconds`** of a stub's count as the
  stub's, for the download and the inbox alike: the stub was made from a
  summary, and its start may be rounded or taken from another clock. An
  exact match is still the usual case.
- A real activity (one with records) at that start time still makes the
  new file a duplicate, as now.
- It costs a request per stub Wahoo or Garmin Connect has, against their
  limits (spec 44), once: after that the slot holds a real recording.
- **Depends on spec 46**, which changes `Wahoo.cpp`: the Developer starts
  this part after `feature/46-wahoo-everything` is merged.

### Showing a stub

- Until it's replaced, the activity screen says so where the map would
  be: "This activity is only a summary: no track or graphs." The summary
  numbers (distance, time, calories) show as now. Without it, an empty
  map looks like a bug in SweatTrails, which is how this spec started.

### README

The README says what a stub is, that dropping Strava's `activities.csv`
replaces them, that downloads replace them too, that the stub is kept as
`.fit.stub`, and the `inbox/duplicates` tip below.

### Version 1 scope

In: the stub test, replacing a stub (keeping it as `.fit.stub`), the
import from Strava's export, the downloads and inbox treating a stub as
missing, and the line on the activity screen.

Deferred:

- GPX and TCX originals from the export *(deferred)*: none of Jan's stubs
  need them.
- Looking through `inbox/duplicates` for originals of stubs that were
  set aside before this *(deferred)*. Moving the files back into the inbox
  does it by hand, and the README says so.
- Strava's API has no original files, only streams, so it isn't a way in.

## Open questions

None.

## Architecture

### Checked against Jan's store and export

The Bermuda stub's sidecar is `{ "start_time": 1740662259, "strava":
{ "id": 13742418979, ... } }`. Its line in `activities.csv` has
`Filename` `activities/14666148179.fit.gz`: the file is named by Strava's
upload ID, not the activity's, so the import goes from the activity ID to
the line's `Filename`, never from the ID to a file name. Besides 5856
`.fit.gz`, the export has some plain `.fit` and `.gpx` files, and
`.gpx.gz`/`.tcx.gz`: a `.fit` is read as it is, anything not FIT counts as
"a GPX or TCX file". Reading every FIT file in the store (5586) takes
1.3 s, so the stub test reads the whole file and doesn't try to be clever.

### gunzip: `src/Lib/Base/Zip.h`

```cpp
// The contents of a gzip file, such as the `.fit.gz` files of Strava's
// export.
std::expected<std::string, LibCError> gunzip(std::string_view gz);
```

zlib's `inflateInit2(16 + MAX_WBITS)`, growing the output as it goes,
with the same guard on the inflate ratio as `unzip()`.

### Stubs: `src/App/storage/Stubs.h`, `Stubs.cpp`

```cpp
// Whether a FIT file has no record messages: an activity that's only a
// summary. Reads until the first record, so a real recording is quick.
std::expected<bool, FITError> is_stub(std::string_view fit);
StorageResult<bool>           is_stub_file(fs::path const &fit);

// What the store has where an activity starting at `start` would go.
struct Slot {
    enum class Kind {
        Free,  // Nothing
        Stub,  // A stub within StravaMatchSeconds, and no real activity at `start`
        Taken, // A real activity at `start`, or one in a .recordings folder
    };
    Kind        kind { Kind::Free };
    StoredStart stub { }; // For Stub: the nearest one
};
[[nodiscard]] Slot slot_for(Storage const &storage, i64 start);

// Replaces the stub with `fit`, the original, which starts at `start`:
// see "Replacing a stub" below. `incoming`, the original's own sidecar
// from a download, if any, fills in what the stub's sidecar doesn't have.
StorageResult<void> replace_stub(Storage const &storage, StoredStart const &stub, std::string_view fit, i64 start, std::optional<JSONValue> const &incoming = { });
```

`slot_for()` looks like `has_activity()`: in the month of `start` and the
months around it, with `month_starts()`, not the whole store, as the
downloads call it once per listed workout. In order: a file at `start`
exactly that isn't a stub is Taken; else the stub nearest `start` within
`StravaMatchSeconds` makes it Stub; else `has_activity()` decides between
Taken and Free (it also sees the `.recordings` folders). A file that can't
be read counts as real: never replace what we can't look at.

`replace_stub()`:

1. Renames `<stub>.fit` to `<dir>/<start>.fit.stub`, `<dir>` being the
   month directory `start` files into. Fails if that exists.
2. Writes the original as `<dir>/<start>.fit` (`write_file_atomically()`,
   after checking it isn't there). If that fails, the stub is renamed back.
3. If `start` isn't the stub's: the sidecar moves to `<dir>/<start>.json`,
   with `"start_time"` set to `start`. If `incoming` has keys the sidecar
   lacks (a Wahoo or Garmin Connect download's), they're added; the stub's
   own keys win. With no stub sidecar, `incoming` becomes the sidecar.

`.fit.stub` has the extension `.stub`, so `Activities::list()`,
`month_starts()` and the summaries pass it by, as they do `.fit.original`.

### The inbox: `Storage::organize_fit_file()`

`InboxOutcome` gets `Replaced`. `organize_fit_file()` asks `slot_for()`
instead of `has_activity()`: Taken is a duplicate, as now; Stub, when the
incoming file has records, calls `replace_stub()` with the file's bytes
and the inbox sidecar, then removes both from the inbox; Free files it as
now. An incoming stub is filed or set aside as now, never replaces a stub.
`process_inbox()` counts Replaced as processed and calls `on_filed()`
for it, so the job groups the new recording with any others
(`group_new()`), and updates the summaries.

### The downloads: `Wahoo.cpp`, `Garmin.cpp`

Their test `!storage.has_activity(start)` becomes `wants_activity(storage,
start)`, `slot_for(...).kind != Taken`. That's a one-line change in each;
the Wahoo one waits until `feature/46-wahoo-everything` is merged, as the
spec says.

### Strava's export: `StravaExport.h`

`export_descriptions()` becomes `read_export()`, as the line now gives
more than the description:

```cpp
struct ExportActivity {
    std::string description { };
    std::string file { }; // The line's Filename: "activities/14666148179.fit.gz", or empty
};
std::expected<std::map<u64, ExportActivity>, std::string> read_export(std::string_view csv, std::string_view file_name);
```

A CSV without a `Filename` column still imports descriptions; it just
has no originals. `import_descriptions()` takes the new map. New:

```cpp
struct StubImport {
    size_t                                replaced { 0 };
    size_t                                unreadable { 0 }; // GPX or TCX originals
    std::vector<std::pair<i64, i64>>      moved { };        // (stub's start, original's start)
};
// Replaces each stub with a Strava ID whose export line has a FIT file
// in `export_dir`. Runs on the job thread.
StorageResult<StubImport> import_originals(Storage const &storage, std::map<u64, ExportActivity> const &activities, fs::path const &export_dir, ExportProgress const &progress = { });
```

It first collects the activities with a Strava ID whose line has a
`Filename`, and is_stub_file() each (only those: the 1.3 s above is the
worst case), then replaces them with progress. An original that is
missing, can't be read, or is a stub itself is logged and skipped,
not counted. `ExportImport` gets `replaced` and `unreadable`, and
`message()` adds the spec's two sentences.

### The app

- The `ImportStravaExport` job runs `import_originals()` after the
  descriptions. `ImportingDescriptions` gets a `bool stubs`, for the
  status line "Replacing activities that are only a summary: 12 of 140".
- A new notification `StubsReplaced { moved }`, sent by the import and,
  for Replaced files, by the inbox job: if the activity on display is one
  of the stubs, it's loaded again under its new start (`show_again()`,
  which also updates the summaries). The import calls `update_summaries()`
  itself when it replaced any.
- The import also calls `group_new()` for each replaced start, as the
  inbox does, and sends `RecordingsGrouped`.

### Showing a stub: `widget/ActivityMap.cpp`

`Activity::is_stub()`: a deep-loaded activity without records. In
`ActivityMap::set_activity()`, a stub keeps the map's space instead of
hiding it (as an indoor activity without positions does), with
`has_map` false and `stub` true; `draw()` writes "This activity is only a
summary: no track or graphs." in the middle. Too small and specific to be
a widget in `src/Lib/Widget`.

### README

A section under Strava's export, as the spec says.

### Changed while building

- `process_inbox()` takes a third callback, `InboxReplaced`, called with
  the stub's start and the recording's for each replacement, after
  `InboxFiled`; the inbox job collects them for `StubsReplaced`.
- `import_originals()` takes a `Storage &`, not a const one: it reads
  each original's start with a shallow `Activity` load, as the inbox
  does, so that both file it by the same start.
- `StubImport` is a member of `ExportImport`, `stubs`, so `message()`
  has it.
- `Activity::is_stub()` looks at `recorded`, the records as read.

### Tried on a copy of Jan's store

With Jan's export (2026-11-11) on a copy of the store:

- **113 stubs, not 191.** 191 FIT files are under 1000 bytes, but 78 of
  them have records: short recordings converted by the old app (maker
  255), a few seconds of GPS, or up to an hour and a half of heart rate
  without positions. They are what was recorded, so by the spec's test
  they are no stubs. 3 of the 113 are in `.recordings` folders, where
  the import doesn't look, as the titles don't.
- Of the 110 listed stubs, 73 have a `.fit.gz` in the export, 31 are
  manual entries on Strava, and 6 have no Strava ID. None has a GPX or
  TCX file.
- **72 replaced**, the Bermuda run among them (7216 records, the
  RIVAL's). None had another start than its stub's. The 73rd,
  2024-06-25, isn't: its recording is already in the store, 37 s later,
  downloaded from Wahoo with a Strava ID of its own, so it's left alone
  with a warning.
- Looking for stubs reads every FIT file with a Strava ID: about 7 s in
  a debug build.

### Order of the work

1. `gunzip()`.
2. `Stubs.h`: `is_stub()`, `slot_for()`, `replace_stub()`.
3. The inbox.
4. Strava's export.
5. The activity screen.
6. Garmin's and Wahoo's downloads; Wahoo's after spec 46 is merged.
7. README.

## Test plan

- `zip_test`: `gunzip()` of a small gzip made in the test with zlib's
  `compress2`-equivalent (`deflateInit2` with gzip header), of an empty
  file, and of garbage and a truncated file (errors).
- `stubs_test` (new, `test/StubsTest.cpp`), on a `TempStore` with FIT
  files made with `TestFit.h`:
  - `is_stub()`: a file with records isn't, one with only a session is,
    and one that isn't FIT is an error.
  - `slot_for()`: empty store Free; real at the start Taken; stub at the
    start Stub; stub 30 s off Stub, 61 s off Free; real at the start and a
    stub 10 s off Taken; two stubs, the nearest; a recording in a
    `.recordings` folder Taken; a stub in the month before (time zones).
  - `replace_stub()`: same start: `.fit` is the original, `.fit.stub` the
    stub, the sidecar untouched; another start: the files under the new
    start, sidecar moved with `"start_time"` updated, nothing left under
    the old one; `incoming` adds missing keys, doesn't overwrite; no stub
    sidecar takes `incoming`; an existing `.fit.stub` is an error and
    changes nothing; the listing and `month_starts()` don't see
    `.fit.stub`.
- The inbox (`stubs_test`): an original for a stub is Replaced, counted,
  its inbox sidecar merged and removed from the inbox, `on_filed()`
  called; an incoming stub over a stub is a duplicate; a real activity
  still makes it a duplicate.
- `strava_export_test`: `read_export()` reads `Filename`, and a CSV
  without that column still reads; `import_originals()` with a temporary
  export directory: a `.fit.gz` and a `.fit` replace their stubs; a
  `.gpx.gz` counts as unreadable; a missing file, an original that is a
  stub, and a real activity with a Strava ID are left alone;
  `ExportImport::message()` with replaced and unreadable.
- `garmin_test`, `wahoo_test`: a listed activity whose start only a stub
  has (exactly, and 30 s off) is downloaded; one at a real activity's
  isn't.
- `Activity::is_stub()` in `stubs_test`.
- By hand: the map's line (drawing), and the import on a copy of Jan's
  store with his export: 140 replaced, the Bermuda run with its map.

## Review notes

*Filled in by the Reviewer.*
