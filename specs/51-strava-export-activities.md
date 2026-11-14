# Importing the activities in Strava's export

Status: In progress, on branch feature/51-strava-export-activities

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

Strava's account export has activities that the store doesn't: ones that
never went through Garmin Connect or Wahoo, such as the years before
either, Zwift rides uploaded straight to Strava, and some races. Dropping
the export's `activities.csv` already brings in descriptions (spec
[45](45-strava-export-import.md)) and replaces the activities that are
only a summary (spec [50](50-stub-activities.md)). It should bring in
the missing activities too, so that Strava's export fills every gap
it can in one go. Since the task then does more than descriptions, it
gets a new name.

## Feature

### What there is now

- **Dropping `activities.csv`** (`st-strava-export`, "Import descriptions
  from Strava's export…") runs the `ImportStravaExport` job: it reads the
  CSV (`read_export()` in `storage/StravaExport.h`), fills in missing
  descriptions by Strava ID (`import_descriptions()`), then replaces stubs
  with the export's FIT files (`import_originals()`). Every line's
  `Filename` is already read: `activities/<upload id>.fit.gz`, or empty
  for an activity entered by hand.
- **The downloads go through the inbox.** Garmin's and Wahoo's write
  `garmin-<id>.fit` and `wahoo-<id>.fit`, each with a `.json` sidecar,
  into `inbox/`, and the `ProcessInbox` job files them
  (`Storage::process_inbox()`): a start time a real activity has is set
  aside in `inbox/duplicates`, a stub's is replaced (spec 50), anything
  else is filed under its start and grouped with other recordings of the
  same activity (`group_new()`, spec [31](31-multiple-recordings.md)).
  `Storage::set_aside_as_duplicates(source)` gives the IDs set aside, so
  that a download doesn't fetch them again.
- `gunzip()` (`Zip.h`) reads the export's `.fit.gz`.

### Jan's export against the store

Jan's export (2026-11-11) has 6281 activities; the store has a Strava ID
for 5152 of them. Of the other 1132:

| What                                         | Count |
|----------------------------------------------|------:|
| A FIT file (`.fit.gz`, 4 plain `.fit`)       |   833 |
| Entered by hand on Strava, no file           |   280 |
| GPX or TCX (`.gpx`, `.gpx.gz`, `.tcx.gz`)    |    18 |
| A `Filename` that's missing from the export  |     1 |

The 833 FIT files, by their own start time against the store's:

| Nearest activity in the store | Count | Likely                                             |
|-------------------------------|------:|----------------------------------------------------|
| Same second                   |   163 | The same activity, uploaded twice or a leg of one  |
| 1 to 60 s off                 |    11 | Another recording of the same activity (spec 31)  |
| 1 min to 1 h off              |   166 | A separate activity the same morning              |
| Nothing within an hour        |   493 | Missing from the store                             |

By type, the files are mostly runs (285), virtual rides (242), rides (140)
and swims (134), from 2010 to 2026.

### The import

- **The missing activities go through the inbox.** For each line of the
  export with a FIT file whose Strava ID no activity in the store has, the
  import puts the file, ungzipped, into the inbox as `strava-<id>.fit`,
  with a sidecar `strava-<id>.json` holding its Strava title, `{ "strava":
  { "id", "name", "description" } }`, as the titles run would save it.
  Then the inbox files them as it does a download's: what's already there
  is set aside, a stub is replaced, the rest is filed and grouped. That
  way an export activity is treated exactly as the same file from Garmin
  or Wahoo would be, and none of the inbox's rules are written twice.
- **Matching is by Strava ID first.** An activity in the store with that
  Strava ID has it, whatever its start time. Only what's left goes to the
  inbox, which then matches by start time.
- **Once set aside, not again.** The inbox sets aside the 163 same-second
  ones as duplicates. A second import skips the IDs
  `set_aside_as_duplicates("strava")` gives, as Wahoo's and Garmin's
  downloads do, so dropping the CSV again doesn't fill `inbox/duplicates`
  twice.
- **Other recordings are kept.** A file within `StravaMatchSeconds` of an
  activity in the store, but not at its second (11 in Jan's export), is
  another recording of it, such as Zwift's file next to the head unit's.
  The inbox files it and groups it with the activity (spec 31), as it
  would the same file from a download. Jan wants these kept.
- **Activities entered by hand are skipped.** The 280 lines without a
  file have only the CSV's totals; made into stubs they'd count in the
  volume summaries, but show "only a summary" on the activity screen.
  Jan doesn't need their totals, so they're left out, and the closing
  message doesn't mention them.
- **The order in the job:** descriptions, then stubs (spec 50), then the
  missing activities. The stub step then only sees what was in the store,
  and a new activity that lands on a stub's slot replaces it through the
  inbox anyway.
- **The job runs the inbox itself**, on the job thread it's already on,
  rather than scheduling `ProcessInbox`, so that the closing message can
  count what was added and what was set aside. It does what the
  `ProcessInbox` job does after filing: grouping, summaries, and the
  notifications for filed and replaced activities. The status line counts
  the files as they go into the inbox, then as the inbox files them:
  "Adding Strava's activities: 120 of 671".
- **The closing message** adds a sentence: "Added 493 activities from
  the export; 163 were already here." The second part only when the inbox
  set any aside. GPX and TCX files of missing activities are counted with
  those of stubs: "18 more have a GPX or TCX file, which SweatTrails can't
  read."
- **Failures**: a file that's missing from the export, can't be gunzipped
  or can't be written to the inbox is logged and skipped, as in spec 50,
  and doesn't stop the import.

### The task's new name

"Import descriptions from Strava's export…" becomes **"Import Strava's
export…"**, `st-strava-export` as now. The message box it shows from the
palette stays: "Unzip the archive Strava emailed you, and drop
activities.csv from it on this window."

### Version 1 scope

In: the export's FIT files for activities the store doesn't have, through
the inbox; the task's new name; the closing message; the README's section
on Strava's export, which says that dropping `activities.csv` also adds
the activities the store doesn't have.

Deferred:

- GPX and TCX files *(deferred)*: SweatTrails reads only FIT, as in spec
  50. The closing message counts them.
- Activities entered by hand on Strava *(deferred)*, as stubs made from
  the CSV's totals; see above.

## Open questions

None.

## Architecture

### Reading the name: `ExportActivity::name`

`read_export()` also reads the `Activity Name` column into a new
`ExportActivity::name`, for the sidecar. The column is optional, as
`Filename` is: without it the name is empty.

### Into the inbox: `queue_missing()` in `storage/StravaExport.h`

```cpp
// What putting the export's missing activities in the inbox did, and
// then what the inbox did with them.
struct MissingImport {
    size_t queued { 0 };     // FIT files put in the inbox
    size_t unreadable { 0 }; // GPX or TCX files
    size_t added { 0 };      // Filed by the inbox, or replacing a stub
    size_t set_aside { 0 };  // Set aside by the inbox as duplicates

    // Counts what the inbox did with `file_name`, if it's one of ours.
    void count(std::string_view file_name, Storage::InboxFiling const &filing);
};

// Puts the FIT file of each line of `activities` whose Strava ID no
// activity in the store has, and that the inbox hasn't set aside
// before, into the inbox as strava-<id>.fit, with its sidecar. Runs on
// the job thread.
StorageResult<MissingImport> queue_missing(Storage const &storage, ExportActivities const &activities,
    fs::path const &export_dir, ExportProgress const &progress = { });
```

- **The Strava IDs the store has** come from every sidecar in the month
  directories, *and* in the `.recordings` folders: a Strava file that the
  first import grouped as another recording (spec 31) has its ID in the
  folder. Without that, a second import would put it in the inbox again,
  and the inbox would set it aside once more.
- The sidecar is written first, then the FIT file, as Wahoo's download
  does. The sidecar is `StravaTitle { id, name, description }.encode()`
  under `"strava"`, through `save_sidecar_strava()`.
- The `.fit`/`.fit.gz`/other split is `original_kind()`, as for stubs.
  Reading and gunzipping is pulled out of `read_original()` into a
  helper both use; `queue_missing()` doesn't load the activity or check
  it's a stub: that's the inbox's business, so that an export file is
  treated as the same file from a download.

### Seeing what the inbox did: `Storage::InboxFiled` changes

`process_inbox()` now only tells about files it filed, by start time,
and replacements separately. To count what was set aside, and to count
only the `strava-` files (the inbox may hold Garmin's or Wahoo's too),
the callback gets the file name and the whole `InboxFiling`, for every
file, whatever the outcome:

```cpp
// Called for each inbox file with what the inbox did with it.
using InboxFiled = std::function<void(std::string_view file_name, InboxFiling const &filing)>;
StorageResult<u32> process_inbox(InboxProgress const &on_file = { }, InboxFiled const &on_filed = { });
```

`InboxReplaced` goes: `InboxFiling` already has `stub_start`. The three
callers (the app's `process_inbox()`, `StubsTest`, `RecordingsTest`)
check the outcome themselves.

### The job

- The app's `process_inbox(the)` in `SweatTrails.cpp` (grouping,
  `StubsReplaced`, `RecordingsGrouped`, summaries) gets two optional
  arguments: an `InboxProgress` that replaces its `ImportingFile` status,
  and an `InboxFiled` it calls as well. It returns the inbox's result,
  and the `ProcessInbox` job says the error as now. So the export job
  runs exactly what the `ProcessInbox` job runs.
- `job_import_strava_export()` runs `queue_missing()` after
  `import_originals()`, then `process_inbox(the, on_progress,
  [&missing](...) { missing.count(...); })`.
- `ImportingDescriptions::stubs` (a bool) becomes `step`, an enum
  `Descriptions`, `Stubs`, `Activities`, for the status line's "Adding
  Strava's activities: 120 of 671".
- `ExportImport` gets a `MissingImport missing`, and `message()` adds
  "Added 493 activities from the export; 163 were already here." The GPX
  or TCX count is `stubs.unreadable + missing.unreadable`, and its
  sentence reads "N more have…" when anything was replaced or added,
  else "N activities have a GPX or TCX file…". Nothing is said about
  activities added when none were.
- An activity the inbox files on a stub's slot (a stub with no Strava ID
  that matches the export's) counts as added, not as a replaced stub:
  the stub step's count stays what the stub step did.
- The activity list is a modal that reads the store when it opens, so
  nothing has to be refreshed after the import beyond what the inbox's
  notifications do.

### The task's name

`.titled("Import Strava's export…")`; the README's section on Strava's
export says it also adds the activities the store doesn't have.

## Test plan

In `test/StravaExportTest.cpp`, with `TempStore` and `TestFit`'s
`recorded_fit()`/`summary_fit()`:

- `read_export()` reads `Activity Name`, and leaves it empty without the
  column.
- `queue_missing()`: an export with lines whose ID is in the store (as a
  listed activity and in a `.recordings` folder), a gzipped and a plain
  FIT file of missing activities, a GPX, a missing file, a corrupt
  `.fit.gz`, a line without a file, and an ID set aside in
  `inbox/duplicates` as `strava-<id>.fit`. Checks what's in the inbox,
  the sidecars' `strava` titles, the counts and the progress calls.
- Through the real inbox: `process_inbox()` with `MissingImport::count`
  files a missing one, sets one at an existing start aside, groups one a
  few seconds off with its activity, and ignores a `garmin-` file in the
  inbox for the counts. A second `queue_missing()` then queues nothing.
- `ExportImport::message()`: added with and without set aside, GPX/TCX
  from stubs and missing activities together, and nothing added.
- `StubsTest` and `RecordingsTest` keep passing with the new callback.

Not tested automatically: the job in `SweatTrails.cpp` and the status
line, which need the app's bus; they're thin over the tested functions.
Jan checks the import on the real export by hand.

## Review notes

*Filled in by the Reviewer.*
