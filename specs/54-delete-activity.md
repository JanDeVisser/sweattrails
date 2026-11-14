# Deleting an activity

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

The user removes an activity they don't want in SweatTrails, such as a
ride recorded by accident, a test file, or a duplicate the recordings
grouping (spec [31](31-multiple-recordings.md)) didn't catch, from the
command palette (spec [36](36-command-palette.md)). A recording of an
activity recorded on several devices can be deleted on its own. What's
deleted is gone from the month's list, Left and Right, the totals and the
fitness, and the downloads don't bring it back.

## Feature

### What there is now

- An activity is `<year>/<month>/<start>.fit` in the store
  (`activity/` in the data folder), with files of the same stem next to
  it: the sidecar `<start>.json`, and maybe `<start>.fit.original` (spec
  30), `<start>.fit.stub` (spec 50) and `<start>.strava` (the old app's).
  `move_recording()` in `storage/Recordings.cpp` already moves that set
  of files together.
- An activity with several recordings has its other recordings in
  `<start>.recordings/` next to it (spec 31).
- The downloads skip a start time `Storage::has_activity()` finds; the
  inbox sets aside a file whose start time is taken. Neither knows about
  anything that was deleted, so today removing the files by hand makes
  the next download within the period (30 days, or "Everything") fetch
  the activity again.
- The summary file (spec 27) drops an activity whose file is gone, and
  the totals screen can be told to reload, as after the inbox.
- There's a yes/no query modal (`QueryOptionYesNo`, `Modal.h`).
- There's no way to delete anything from the app.

### Deleting

- **A new task, `st-delete-activity`**, titled "Delete activity", in the
  palette only: no key, as deleting is rare and a key is easy to hit by
  mistake. It works on what's on display, on the activity screen only
  (`when()`), and not with an empty store.
- **It asks first**, as a yes/no query, with the activity's name and
  date: "Delete “Morning Ride” of 2025-02-27? It's moved to the deleted
  folder." No is the default, so Enter right after opening it does
  nothing.
- **What goes**: the activity's files, `.fit`, `.json`, `.fit.original`,
  `.fit.stub` and `.strava`, as `move_recording()` moves them together.
- **Then** the activity after it is shown, or the one before it if it
  was the last; with none left, the empty screen. The summary file and
  the totals are updated, as after the inbox. The status line says
  "Deleted “Morning Ride”".
- Garmin Connect, Wahoo and Strava aren't touched: this deletes from
  SweatTrails only.

### An activity with several recordings

Deleting works on **the recording on display** (spec
[32](32-switching-recordings.md)), not the whole group: that's the case
deleting a recording is for, such as a head unit's that dropped out, and
the user can delete the others one at a time.

- **The query names the recording**: "Delete recording 2 of 3 (Wahoo) of
  “Morning Ride”?", as the summary line names it.
- **A recording that isn't the main one** goes from the folder; the
  activity stays as it is, and is shown again as its main one.
- **The main one**: another becomes the main one, chosen by spec 31's
  rules (the `recordings.prefer` setting, then the longest), as a main
  one chosen with Ctrl+J was the deleted one's own choice. It's listed
  where the inbox would file it, and the activity's name moves to its
  sidecar unless it has one of its own, as with Ctrl+J. The laps and
  fills made on the deleted one go with it: they're in its times. The
  activity is then shown as its new main one.
- **With one recording left**, it's an activity of its own, and the
  recordings folder goes, as after a split.
- Either way the status line says "Deleted recording 2 of 3 of “Morning
  Ride”".

### Where it goes

- **Into `deleted/` in the data folder**, next to `activity/` and
  `inbox/`, with the same `<year>/<month>/` layout as where it was: the
  files keep their names, their start times, and a recording from a
  folder lands next to the others, flat, not in a `.recordings/` folder.
  Nothing in the app lists it.
- Getting an activity back is moving its files back by hand into
  `activity/`, or into the inbox, which the README says *(an undo or
  restore command deferred)*.
- Nothing is ever removed from `deleted/` by the app; the user empties it
  when they want to.
- **A start time deleted twice**, such as one deleted, dropped in the
  inbox again and deleted again, keeps both copies: the later one is
  named `<start>-1.fit`, `<start>-2.fit`, ..., with its other files, as
  in `inbox/duplicates/`.
- Not the system's Trash: that needs code of its own on macOS and Linux,
  and once the user empties it nothing would remember what was deleted
  (below). Not removing outright: a slip couldn't be undone.

### Not downloading it again

- **The downloads treat a deleted start time as one the store has**:
  `has_activity()` also looks in `deleted/`, in its month and the months
  around it, as it does in `activity/`. So Garmin Connect's and Wahoo's
  downloads, "Everything" included, skip it. The `deleted/` folder is the
  record of what was deleted: there's no separate list to keep in step,
  and emptying it means the downloads may fetch those activities again,
  which the README says.
- **Importing Strava's export skips a deleted start time** too: it puts
  activities in the inbox the user didn't ask for one by one, as a
  download does.
- **A file put in the inbox at a deleted start time is filed**, as if it
  had never been deleted, and the deleted copy stays in `deleted/`:
  dropping a file is asking for it. The downloads check `has_activity()`
  before fetching, so only a file the user put there gets this far. If it
  turns out to be a recording of another activity, it's grouped, as any
  file from the inbox is.

### README

How to delete an activity or a recording, where it goes, how to get it
back, and that emptying `deleted/` lets the downloads fetch them again.

### Version 1 scope

Everything above. Deferred:

- Undo, or a restore command *(deferred)*.
- Deleting several activities at once, such as from the month's list
  *(deferred)*.
- Deleting on Garmin Connect, Wahoo or Strava *(deferred)*.

## Open questions

None.

## Architecture

### Storage

- **`Storage::deleted_dir`**, `deleted/` in the data folder, set by
  `init()` as the other folders are, but made only by the first delete.
- **`Storage::was_deleted(start)`**: whether `deleted/` has
  `<start>.fit` in the start's month or the months around it, as
  `fit_at()` looks in `activity/`. Flat: there are no `.recordings/`
  folders in `deleted/`.
- **`has_activity()` becomes `fit_at() || was_deleted()`.** The downloads
  ask `wants_activity()`, which ends in `slot_for()`, which ends in
  `has_activity()`, so Garmin Connect and Wahoo skip a deleted start with
  no change of their own.
- **Strava's export**: `queue_missing()` skips a file whose start
  `was_deleted()`, before it queues it for the inbox.
- **The inbox asks `fit_at()`** instead of `has_activity()` before
  setting a file aside as a duplicate, so a file dropped at a deleted
  start is filed. `slot_for()` only matters to the inbox for a stub,
  which a deleted start never is.

### Deleting, in `storage/Recordings.{h,cpp}`

Deleting the main recording is regrouping, which is what
`Recordings.cpp` does, and it needs its helpers (`move_recording()`,
`filed_path()`, `in_folder()`, the sidecar's title), so it goes there:

```cpp
struct Deleted {
    fs::path                to { };   // The FIT file in deleted/
    std::optional<fs::path> main { }; // The activity's main one after, if it's still there
};
StorageResult<Deleted> delete_recording(Storage &storage, fs::path const &fit);
```

- **An activity of one recording**: `move_recording()` into
  `deleted/<year>/<month>/`, the month it was in. `main` is none.
- `move_recording()` gets an optional destination stem, so that a start
  already in `deleted/` is moved as `<start>-<n>`, the first free `n`.
  `was_deleted()` finds the first copy, `<start>.fit`.
- **A recording in a folder**: moved into `deleted/`, under its main
  one's month, flat. If the folder is empty after, it's removed. `main`
  is the main one, unchanged.
- **The main one of several**: its own title is read first, then it's
  moved into `deleted/`, and only then are the others regrouped, so that
  a failure leaves the store with the others still in the folder rather
  than with two mains. The others are regrouped by
  `group_recordings()`, which picks the main one by
  `main_recording()`'s rules (no recording in a folder is `chosen`:
  grouping cleared that), moves it to where the inbox files it, and the
  rest into its folder. With one left, `group_recordings()` of one
  recording just moves it out: an activity of its own. The old folder is
  removed. The title goes to the new main one's sidecar unless it has its
  own, factored out of `group_recordings()` into a helper both use.
- The one left on its own isn't marked `separate`: nothing is left to
  group it with, and if the user puts the deleted one back through the
  inbox, it should group again.

### The query box's default, in `src/Lib/Widget/Modal.h`

`query_box()` selects the first option, Yes. It gets a last parameter,
`QueryOption initial`, the option selected when it opens, defaulting to
the first, so the existing calls don't change. Selected after `show()`,
as `list_box()` does with its `initial`.

### The app

- **Texts**, in `widget/DataFields.{h,cpp}` next to `device_name()` and
  the summary line's recording suffix, so a test reaches them:
  `delete_question(Activity const &)` and `deleted_status(Activity const
  &)`. The date is the start's, `YYYY-MM-DD`; the name is the activity's
  `title`, which is the activity's whichever recording is shown. The
  question for a recording also says "It's moved to the deleted
  folder.", as the activity's does.
- **`st-delete-activity`**, titled "Delete activity", no key,
  `.when()` on the activity screen with an activity on display (none
  with an empty store). `cmd_delete_activity()` asks with
  `query_box(..., QueryOptionYesNo, QueryOptionNo)`, and Yes schedules a
  job.
- **`JobType::DeleteActivity`**, with a `DeleteRequest`: the shown ID
  and FIT file, as Ctrl+J's `RecordingsRequest`, and the status text,
  made on the UI thread from the activity, which the job doesn't have. The job runs on
  the job thread because it changes the store and rescans. For an
  activity of one recording it asks `activity_next_to()` (+1, else -1)
  *before* deleting: after, the ID isn't listed, and `activity_next_to()`
  would skip the rest of its month. It then calls `delete_recording()`,
  rescans, and notifies.
- **`ActivityDeleted`**, a new notification: the ID to show, if any, the
  status text, and an error. Its handler shows the ID as `show_again()`
  does (which also updates the summaries), or, with none left, clears
  the display (`ActivityDisplay::clear_activity()`) and updates the
  summaries. The fitness is computed from the summaries, so it follows.

### README

A section on deleting, as the spec says.

## Test plan

- **`DeleteTest`**, a new test of its own (storage, in a `TempStore`,
  which gets a `deleted_dir`):
  - Deleting an activity of one recording moves all five kinds of file
    into `deleted/<year>/<month>/`, keeping names; nothing is left in
    `activity/`; the month no longer lists it.
  - Deleting a recording in a folder of three: it lands flat in
    `deleted/` under the main one's month; the folder keeps the other.
  - Deleting a recording in a folder of two: the folder goes; the main
    one stays, with its sidecar unchanged.
  - Deleting the main one of three: the new main one is the preferred
    device's, else the longest; it's filed where the inbox would file it,
    with the other in its folder; the title moves to it, but not over its
    own; the old folder is gone; `main` names it.
  - Deleting the main one of two: the other is an activity of its own,
    no folder left.
  - Deleting a start that's in `deleted/` already keeps both, the new
    one as `<start>-1.*`.
  - `has_activity()` is true for a deleted start, also one in the month
    next to it, and `wants_activity()`, which the downloads ask, is
    false; `fit_at()` doesn't find it.
- **`StravaExportTest`**: the import doesn't queue an activity whose
  start was deleted.
- **Inbox** (`DeleteTest`): a file dropped at a deleted start is filed,
  not set aside, and the deleted copy stays.
- **`DataFieldsTest`**: `delete_question()` and `deleted_status()` for
  an activity, and for recording 2 of 3 with and without a device name.
- **`QueryBoxTest`**, new: Enter answers with the first option, or with
  `initial`, or the first if `initial` isn't offered.
- **Not automated**: the palette task, the job and the notification
  handler live in `SweatTrails.cpp`, which needs a window; checked by
  hand in the app: deleting the middle, the last and the only activity of
  a month, the last one in the store, and each recording case.

## Review notes

1. **Bug**, `job_delete_activity()`, `src/App/SweatTrails.cpp`: when
   `delete_recording()` fails after the main one of several recordings was
   moved (the regrouping failed, e.g. a rename error), `main_of(request.fit)`
   no longer exists and `next` is empty, because it's only computed for an
   activity of one recording. `ActivityDeleted.show` is then empty, and
   `notif_activity_deleted()` takes the "store is empty" path: it clears the
   display and `last_activity` although the store still has the other
   recordings and all other activities. Fix: on failure with no main left,
   show the first of `recordings_of()`/a rescanned neighbour, or compute
   `next` before deleting for every main-recording case. Not testable
   without a window; the storage half is covered by
   `test_delete_the_main_one_unreadable_other`.
   **Resolved** in 606890c: the job finds the activity next to it for
   every main recording, before deleting, and shows that one if no main
   one is left. Still in that failure, the others stay in the old
   `.recordings/` folder without a main one, so nothing lists them until
   they're moved by hand. Left as it is: it takes a rename failing
   halfway, and undoing it would move files back from `deleted/`, which
   can fail too.
2. **Suggestion**, `queue_missing()`, `StravaExport.cpp`: the `with_id.empty()`
   branch now reads every missing file through `recording_info()` just to
   ask `was_deleted()`, before `queue()` copies it. With "Everything" and a
   large export that is a second full parse per file. Checking the start from
   the export's own metadata, if it has it, would avoid that.
   **Not changed**, agreed with Jan: after the first import few files get
   that far, and the export's date column is text that may not match the
   FIT file's start to the second, so it can't stand in for reading it.
3. **Missing test, added**: `test_delete_missing` (file not in the store:
   error, nothing moved, no `deleted/` made),
   `test_delete_the_main_one_unreadable_other` (an unreadable other recording
   leaves the store untouched, as the Architecture promises) and
   `test_delete_another_recording_twice` (a folder recording whose start is
   in `deleted/` already becomes `<start>-1.fit`). All pass.
4. **Not checked**: the palette task, job and handler (needs a window),
   including deleting the middle, last and only activity of a month, and the
   last one in the store. The code reads right for those, apart from 1.

Everything else in Version 1 scope matches the spec: `has_activity()` /
`was_deleted()`, the inbox using `fit_at()`, `query_box`'s `initial`, the
texts, the README. `ninja` and `ctest` pass, except `oauth_test` (known flaky).
