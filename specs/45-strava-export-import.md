# Importing Strava's account export

Status: In review, on branch feature/45-strava-export-import

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

Bring in the titles and descriptions of a whole Strava history at once,
from the account export Strava makes on request, instead of a request
per description. Jan has some 4600 descriptions waiting, which Strava's
daily limit spreads over five days or more. Split off from
[44](44-rate-limits.md), which deals with the limits for what comes
after.

## Feature

### What there is now

- **Strava's titles** are in each activity's sidecar under `"strava"`:
  `StravaTitle` (`storage/Activity.h`), with Strava's ID, the name, and
  the description, an `std::optional`: none until it's fetched, and an
  empty string for an activity that has none. A run of Strava's titles
  (Ctrl+G, Ctrl+Shift+G, Everything; spec 24 and `StravaTitles.cpp`)
  matches Strava's list to the store by start time, within
  `StravaMatchSeconds`, and saves the ID and name. The description takes
  a request per activity, which is what Strava's limits spread over days.
  Spec [44](44-rate-limits.md) has them fetched in the background.
- **The user's own description** is `"notes"` in the sidecar (spec 41)
  and wins over Strava's. Nothing here touches it.
- **Dropping files on the window** (`SweatTrails::files_dropped()`)
  always submits `st-fill-gaps` (spec 30), which says so when what's
  dropped isn't a FIT file.
- `Zip.h` in `src/Lib/Base` reads a zip held in memory, without ZIP64.
  There's no CSV reader.

### Strava's export

- The user asks Strava for it (strava.com: Settings, My Account,
  Download or Delete Your Account, Request Your Archive), and gets an
  email with a link to a zip some time later. In the zip, next to the
  original files of every activity, is `activities.csv`: a line per
  activity with its ID, date, name, type and description, among many
  other columns.
- A description can have commas, quotes and line breaks, so the file
  needs a real CSV reader (RFC 4180: quoted fields, `""` for a quote,
  line breaks inside quotes). The columns are found by their headings,
  `Activity ID` and `Activity Description`; some other headings occur
  twice, which doesn't matter for those two.

### The import

- **The user unzips the export and drops `activities.csv` on the
  window.** A dropped file named `*.csv` goes to the import; anything
  else goes to the gap filling, as now. The zip itself isn't read: it
  holds the original file of every activity, gigabytes for a long
  history, which would need ZIP64 and reading from disk, neither of
  which `Zip.h` does.
- **A palette task, "Import descriptions from Strava's export…"**, no
  key, says how: a message box, "Unzip the archive Strava emailed you,
  and drop activities.csv from it on this window." Dropping is easier
  than typing a path in an input box, and the task is there so that the
  feature can be found.
- **Descriptions are filled in by Strava's ID.** Each activity in the
  store with a Strava title but no description (`description` is
  `std::nullopt`) gets the export's description for that ID, saved in
  the sidecar's `"strava"`, as a fetched one is. An empty one in the
  export is saved as empty, which says the activity has none, so nobody
  asks Strava for it again. Most activities have no description, so
  this is likely most of the backlog.
- **Only what's missing is filled in.** A description already fetched
  is kept: it's as new as the export or newer. Names aren't touched: the
  list, which is cheap, keeps them up to date. The user's own
  description, `"notes"`, isn't touched either.
- **Activities without a Strava title aren't matched by date.** The
  export's dates are formatted for reading, such as "Mar 31, 2013,
  12:36:42 PM", possibly in the account's language, and matching by
  date is what Strava's list already does, for some 30 requests for a
  whole history. So the closing message says how many activities have
  no Strava title, and to get all the titles from Strava first.
- **It runs as a job**, as a download does, with the status line
  counting: "Importing Strava's descriptions: 1200 of 4644".
- **The closing message** says what came in: "Strava's export: 4502
  descriptions, 3911 of them empty. 142 activities have no Strava
  title: if Strava has them, get all the titles (Ctrl+Shift+G) and import
  again." The second sentence only when there are such activities. It
  says "if Strava has them" because activities Strava doesn't have, such
  as Jan's some 325, stay without a title however often the titles run.
- **A file that isn't Strava's export**, without the `Activity ID` and
  `Activity Description` headings, or that isn't valid CSV, imports
  nothing, and the message says so: "activities.csv isn't Strava's list
  of activities: it has no Activity Description column."
- The descriptions that come in show on the activity screen at once, as
  fetched ones do (`TitlesChanged`).

### Version 1 scope

In: the import of descriptions by ID from a dropped `activities.csv`,
the palette task, and a CSV reader in `src/Lib/Base`. Deferred: reading
the zip *(deferred)*; matching by date, for activities without a Strava
title *(deferred)*; anything else in the export, such as gear or the
original files *(deferred)*.

**Checked against a real export.** Jan has asked Strava for an export. The
column headings and quoting above are from what's known of the format;
before finishing, the Developer checks them against Jan's
`activities.csv`, including a description with line breaks and quotes,
and makes a few of its lines, with made-up descriptions, into a test
fixture.

## Open questions

None.

## Architecture

### Checked against the real export

Jan's export (2026-11-11, 6281 activities) agrees with the spec:
`Activity ID` is column 0 and `Activity Description` column 4, each
once (`Elapsed Time`, `Distance`, `Max Heart Rate`, `Relative Effort`
and `Commute` occur twice). UTF-8 without a byte order mark; records
end in LF; 439 descriptions have line breaks, as CRLF inside the
quotes, and 28 have quotes. The 49 descriptions already fetched
from Strava's API that the export also has are the same, byte for byte,
CRLFs included, so the export's are saved as they are. The three that
differ were edited after the export, which "only what's missing" covers.
Every row has an ID, and no ID occurs twice.

### CSV reader: `src/Lib/Base/CSV.h`, `CSV.cpp`

```cpp
using CSVRow = std::vector<std::string>;

struct CSV {
    CSVRow              headings { };
    std::vector<CSVRow> rows { };

    // The column with this heading, the first if there are several.
    [[nodiscard]] std::optional<size_t> column(std::string_view heading) const;

    // RFC 4180: fields quoted or not, "" for a quote in a quoted field,
    // line breaks in quoted fields. Records end in LF or CRLF; a UTF-8
    // byte order mark at the start is skipped. The first record is the
    // headings.
    static std::expected<CSV, LibCError> parse(std::string_view text);
};
```

A quote that isn't closed, or text after a closing quote other than a
comma or the end of the record, is an error, with the line it's on. A
quote inside an unquoted field is kept as it is, as most readers do. A
row may be shorter than the headings (`column()` is then past its end,
which the import checks).

### The import: `src/App/storage/StravaExport.h`, `StravaExport.cpp`

Two steps, so that the parsing is tested without a store:

```cpp
// The export's descriptions by Strava's ID, or why `csv` isn't Strava's
// list of activities: "activities.csv isn't Strava's list of activities:
// it has no Activity Description column."
std::expected<std::map<u64, std::string>, std::string> export_descriptions(std::string_view csv, std::string_view file_name);

struct ExportImport {
    size_t           descriptions { 0 }; // Saved
    size_t           empty { 0 };        // Of those, empty
    size_t           untitled { 0 };     // Activities with no Strava title
    std::vector<i64> changed { };        // By start time, for TitlesChanged
};

using ExportProgress = std::function<void(size_t index, size_t count)>;

// Gives each activity in the store with a Strava title but no
// description the export's description for its ID. Runs on the job thread.
StorageResult<ExportImport> import_descriptions(Storage const &storage, std::map<u64, std::string> const &descriptions, ExportProgress const &progress = { });
```

`import_descriptions()` walks every month with
`months_with_activities()` and `month_starts()`, reads each sidecar with
`sidecar_titles()`, and saves with `save_sidecar_strava()`, as the
titles run does. A sidecar that can't be saved is logged and skipped, as
there. The progress counts the activities to fill in, so the status line
reads "1200 of 4644" over the saves, which are the slow part.

`ExportImport::message()` makes the closing message, so it's tested
too.

### The app

- **Job** `ImportStravaExport`, payload `StravaExportRequest { fs::path
  file }`. It reads the file, calls the two functions, and notifies.
- **Notification** `ImportingDescriptions { index, count, done }`: sent
  with `notify_latest` for progress (status line "Importing Strava's
  descriptions: 1200 of 4644"), and once at the end with `done`, the
  closing message or the error, which goes to a message box and clears
  the status line. `TitlesChanged` is sent with `changed`, as the titles
  run does.
- **Task** `st-strava-export`, titled "Import descriptions from Strava's
  export…", no key. Without arguments, from the palette, it shows the
  message box saying how; with `paths`, from a drop, it submits the job.
- **`files_dropped()`** submits `st-strava-export` when the first path
  ends in `.csv` (any case), and `st-fill-gaps` otherwise, as now.

No new dependencies, no new file formats: the sidecar's `"strava"` is as
spec 24 has it.

### Tried on a copy of Jan's store

On a copy of the store's sidecars (5048 activities), with Jan's export:
4644 descriptions saved, 3085 of them empty, in 3.6 seconds, each the
same as the export's; nothing else in any sidecar changed; 216
activities have no Strava title. A second import saved none. The 26
sidecars with a Strava title that it leaves without a description are
the other recordings of grouped activities (spec 31), in `*.recordings/`
folders, which the titles run doesn't go into either; one that becomes
the main recording is filled in by the next import.

## Test plan

- `test/CSVTest.cpp` (`csv_test`): plain fields; quoted fields with
  commas, `""`, LF and CRLF inside; CRLF and LF record ends; a last record
  without a line end; empty fields, an empty last field, empty quoted
  field; a byte order mark; duplicate headings and `column()` finding the
  first; a missing heading; an unclosed quote and text after a closing
  quote are errors with their line; an empty file.
- `test/StravaExportTest.cpp` (`strava_export_test`), with a fixture
  `test/data/strava-activities.csv`: the real export's headings and a
  few lines of it, descriptions made up, among them one with CRLF line
  breaks and quotes, one with commas, and empty ones.
  - `export_descriptions()`: the fixture's IDs and descriptions; a CSV
    without `Activity Description`, one without `Activity ID`, and one
    that isn't valid CSV, each with its message; a row too short for the
    description column; an ID that isn't a number.
  - `import_descriptions()` on a `TempStore`: an activity with a title
    and no description gets the export's, an empty one is saved as `""`;
    one with a description keeps it; its name and `"notes"` aren't
    touched; one without a title is counted in `untitled`; one whose ID
    isn't in the export is left as it is; `changed` holds what was saved;
    the progress counts up to the number to fill in.
  - `ExportImport::message()`: with and without untitled activities.
- `is_csv_file()`, which decides where a dropped file goes.
- `files_dropped()` and the job are thin. Checked by hand: the whole
  export parses the same as Python's `csv` module (all 6281 rows), and
  the import on a copy of Jan's store, above.

## Review notes

*Filled in by the Reviewer.*
