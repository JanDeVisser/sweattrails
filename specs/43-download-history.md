# Download history

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

Get a whole history of activities from Garmin Connect and Wahoo into
SweatTrails, not just the last 30 days: when the store is empty, as on a
new machine or after moving the activity folder away, and whenever the
user wants an older period. Builds on [07](07-garmin-download.md),
[08](08-wahoo-download.md) and the command palette
([36](36-command-palette.md)).

## Feature

### What there is now

- Every download, at startup (`st-download` with `quiet`) and on Ctrl+G,
  looks back `garmin.days`, `wahoo.days` and `strava.days` days, 30 by
  default (`make_download_request()`, `storage/Download.cpp`).
- `st-download`, `st-garmin-download` and `st-wahoo-download` take a
  `days` argument that overrides those settings, but nothing in the UI
  passes it: the palette runs tasks without arguments. Going further back
  today means editing the settings.
- So an empty store gets the last 30 days and no more.
- The store is `activity/` in the data folder
  (`~/Library/Application Support/sweattrails` on macOS,
  `~/.local/share/sweattrails` on Linux), next to `inbox/`, `tiles/`
  and `summaries.json`.
- Garmin's list takes a `startDate` and pages 100 at a time; Wahoo's has
  no date filter, newest first, 30 a page, and the download stops at the
  first workout older than the period. An activity the store already has
  isn't downloaded again, so a period that overlaps the store costs only
  the list requests.
- `ListBox` (`Modal.h`) is a modal list to pick from.

### Choosing a period

- **A new task, `st-download-period`**, titled "Download activities
  from…", in the palette, shows a list to pick from:
  - The last 30 days
  - This year (since January 1)
  - The last year (365 days)
  - Everything
- "The last year" is the last 365 days, not the previous calendar
  year: a calendar year would need an end date as well as a start, which
  the downloads don't have, and Wahoo's list, newest first without a date
  filter, would page through this year to get to it. Since what the store
  has is skipped, the last 365 days fills in last year's gaps just as
  well.
- Picking one runs `st-download` for that period, from every source
  that's set up (Garmin Connect, Wahoo, and Strava's titles), with the
  same status line and closing message as Ctrl+G.
- Ctrl+G and startup stay as they are: the last N days.
- "Everything" for Garmin Connect is a `startDate` far enough back to
  cover any account, or none if the API allows that; Wahoo pages to the
  end of its list. The Developer checks with a real download which works.

### A long download

- "Everything" can be thousands of FIT files. The status line counts, as
  for any download ("Downloading 412 of 2300 from Garmin Connect").
- **The status line says the download has started**, as before the first
  file there can be a wait, found when Jan tried it: the download can wait
  behind other jobs, and Garmin Connect's list of a whole history is
  some 20 requests. Picking a period, or Ctrl+G, says "Starting the
  download…" at once, and each source, as it's asked for its list, "Asking
  Garmin Connect what there is to download…". The quiet download at
  startup doesn't say it's starting, but does say what it asks.
- **Downloads are paced.** Tried by hand, a download of a whole Garmin
  Connect history stopped part way with an error: Garmin Connect throttles
  a client that asks for too many files in a row, and the download took
  any HTTP error but 403 and 404 as the end. So a download from Garmin
  Connect or Wahoo pauses a minute after every 25 FIT files. A download
  of 25 or fewer, as Ctrl+G's usually is, doesn't pause. While it pauses
  the status line says so: "Pausing a minute between downloads from
  Garmin Connect". About 2,000 activities take some 80 pauses, an hour
  and a half on top of the downloads.
- **A service that still says it's had too many requests** (HTTP 429) for
  a FIT file gets a minute's wait, the same status line, and the file is
  asked for again, up to 3 times, before the download stops with the
  error as before. The activity lists aren't paced or retried: they're a
  request per 100 (Garmin) or 30 (Wahoo) activities, and Wahoo's limit on
  them resets after hours, not a minute.
- **Activities Garmin Connect has no FIT file for are skipped and
  remembered.** Older ones were often uploaded as TCX or GPX files, or
  entered without a file, such as treadmill runs and pool swims, for
  which Garmin Connect answers HTTP 500. A download skips them, with a
  warning in the log, and writes them to `garmin-unavailable.txt` in the
  data folder, a line each with the activity's ID and why, so that the
  next "Everything" doesn't ask for them again. Deleting the file asks
  for them all again. Errors that say Garmin Connect itself is in trouble
  (502 to 504) are skipped but not remembered. Importing TCX files is a
  matter for a spec of its own *(deferred)*.
- **Strava is asked again when it doesn't answer**, in time or at all, or
  answers with a server error: after 30 seconds, 3 times, with "Strava
  didn't answer: asking again shortly" on the status line.
- There's no way to stop a download but quitting, which also works while
  it pauses. Nothing is lost: what came in is in the inbox and is
  processed at the next start, and running "Everything" again skips what
  the store has, so it picks up where it stopped for the cost of the list
  requests. A stop command *(deferred)*.

### Version 1 scope

In: the `st-download-period` task and its list of four periods, the list
at startup for an empty store, and pacing the downloads of FIT files. Deferred: a way to stop a running
download *(deferred)*; calendar years, such as all of 2025, which need
an end date *(deferred)*.

## Open questions

None.

## Architecture

### What's there

- Every download is already a number of days: `st-download` takes a
  `days` argument (`make_download_request()`, `storage/Download.cpp`)
  that Garmin turns into a `startDate` (`garmin_start_date()`), Wahoo
  into the `since` its paging stops at, and Strava into `after`. So a
  period is a number of days, and nothing below the task changes.
- `list_box()` and `PickBox` (`Modal.h`) show a list of texts with
  payloads and call back with the one picked. A task submitted from the
  callback is queued, and runs after the modal is gone, as the Wahoo
  login's Yes does.
- At startup, `SweatTrails` schedules `ProcessInbox`, `GroupRecordings`
  and `FindNewestActivity`, and submits `st-download` with `quiet`.
  `job_find_newest_activity()` says "No activities found" when the store
  is empty.

### Decisions

1. **A period is days, worked out when the list opens.** In
   `storage/Download.h`:

   ```cpp
   struct DownloadPeriod {
       std::string text { };
       int         days { 0 };
   };

   // The periods "Download activities from…" offers, each with the days
   // it looks back from `now`, in local time.
   std::vector<DownloadPeriod> download_periods(i64 now);
   ```

   - The last 30 days: 30.
   - This year: the day of the year plus one (`tm_yday + 1`), so that
     Wahoo's `since`, which keeps the time of day, doesn't cut off
     January 1's morning. Garmin's `startDate` then is December 31, a
     day early, which costs nothing: what the store has is skipped.
   - The last year: 365.
   - Everything: the days back to January 1, 1990
     (`EverythingSinceYear`), plus one as for this year,
       before any GPS watch's FIT file. Checked with a real request on
     2026-11-11: Garmin Connect takes `startDate=1990-01-01`, and lists
     the same as without one.

   No enum: the list's payload is the days, and `st-download` already
   takes them. `offer_download_periods(prompt)` in `SweatTrails.cpp`
   shows the list, for the task and for the startup.
2. **`st-download-period`**, titled "Download activities from…", no key:
   `list_box()` with `download_periods(now)`, prompt "Download activities
   from…"; picking one submits `st-download` with `{ "days": n }`. Not
   under another modal or while a download runs, as `start_download()`.
3. **Which sources bring activities:** in `storage/Download.h`,

   ```cpp
   // Whether a source that brings in activities is set up: Garmin Connect
   // with a key, or Wahoo with a client ID. Strava brings only titles.
   bool downloads_activities(JSONValue const &settings, bool has_garmin_key);
   ```

   The Garmin key is read from the environment and file by the caller,
   with `garmin_key()`, which `download_from_garmin()` now uses too, so
   the function is testable.
4. **The startup check.** `FindNewestActivity`'s payload changes from
   `std::monostate` to `FindNewest { bool at_startup { false }; }`. At
   startup the job, after showing the newest activity, notifies a new
   `StoreChecked { bool empty; }`, and its handler on the UI thread
   decides:
   - empty, and `downloads_activities()`: the period list, with the
     prompt "There are no activities yet. Download from…". Esc
     downloads nothing.
   - otherwise: `st-download` with `quiet`, as now. For an empty store
     with no source, "No activities found" stays as it is.

   So the startup download is submitted after the store is looked at,
   not before; jobs run in order, so it runs at about the same point as
   now. The exception is the once-over of an older store for recordings
   (spec 31), whose chunks schedule each other: the startup download
   waits for the last of them, once. `run_downloads()` keeps scheduling `FindNewest { }`, not at
   startup, which says "No activities found" as before.

   As the quiet download is now submitted after the startup's messages
   (such as "No activities found", or a failure to read the store),
   `start_download()` lets a quiet download run under a modal: the
   modal check is there for Ctrl+G, a key pressed under one.
5. **The Wahoo login keeps the period.** Picking "Everything" without a
   Wahoo login ends with "Log in to Wahoo now?", and the login then
   downloads only `wahoo.days`. `run_downloads()` puts the request's
   `days` in the report's `login_arguments`, as `run_strava_titles()`
   does, with `login_arguments(request, source)` in `storage/Download.h`,
   and `st-wahoo-login` takes a `days` argument, falling back on
   `wahoo.days`.

6. **Pacing**, added after Jan's first try. `storage/Pacing.h`:

   ```cpp
   // How a download of many files is paced: see
   // specs/43-download-history.md.
   struct Pacing {
       size_t               batch { 25 };
       std::chrono::seconds pause { 60 };
       int                  retries { 3 }; // For HTTP 429, per file
       std::function<void(std::chrono::seconds)> wait { }; // Sleeps
   };

   // Whether to pause before the file at `index`: after every batch.
   bool pause_before(Pacing const &pacing, size_t index);
   ```

   `download_new_activities()` and `download_new_workouts()` take a
   `Pacing` and use it the same way: a pause before each batch after the
   first, and on a 429 for a FIT file, a wait and the same file again, up
   to `retries` times. Without a `wait`, nothing waits, so the tests'
   default is fast; they pass a `wait` that counts. The app's `wait`
   sends the status line's message and sleeps with
   `std::this_thread::sleep_for`: the job thread is the download's, and
   blocks as it does for a request, and quitting doesn't wait for it.

7. **The status line's texts** are `download_status(step, source, index,
   count)` in `storage/Download.h`, with `DownloadStep` (Starting,
   Listing, Downloading, Pausing), which the `Downloading` notification
   carries in place of a `pausing` flag. `start_download()` sets Starting
   on the UI thread, as the job may not run for a while.

8. **Unavailable activities**, added after Jan's runs: `GarminError::Kind::NotFit`
   for a download without a FIT file; `unavailable()` in `Garmin.cpp`
   skips it and any HTTP error for one activity but 401 and 429;
   `unavailable_for_good()` (TCX, 403, 404, 500) has it appended to
   `garmin_unavailable_file()`, which `read_unavailable()` reads before
   the next download. Activities set aside in `inbox/duplicates/` are
   skipped too: their start time on Garmin Connect isn't the store's.
9. **Strava's retries** are in `Strava::get()`: `retries`, `retry_pause`
   and a `wait` the app sets to say so on the status line and sleep, as
   Pacing's. `http_fetch()` logs the URL of a failed request instead of
   putting it in the error.
10. **List boxes wrap a long prompt**: `layout_prompt()` in `Modal.h`
    widens the box up to 80% of the window and wraps past that, as a
    long error message made the message box wider than the window.

Nothing else new in `src/Lib/Widget`: `list_box()` is the widget.

## Test plan

In `test/DownloadTest.cpp`, which already tests the download's request
and report:

- `download_periods()` with `TZ` set to Toronto and a fixed `now`, as
  `GarminTest.cpp` does for `garmin_start_date()`: four entries in
  order; 30 and 365; this year on January 1, in March after the change
  to summer time, and on December 31 late enough to be the next year in
  UTC, and that Garmin's start date for it is December 31 of the year
  before; everything's start date is 1989-12-31. The test works out the
  start date as `garmin_start_date()` does, as `download_test` doesn't
  link `Garmin.cpp` and what it pulls in.
- `downloads_activities()`: a Garmin key, a Wahoo client ID, both,
  neither, and Strava alone, which is false.
- `login_arguments()`: Wahoo's and Strava's days, and nothing for
  Garmin Connect, which has no login.
- Pacing, in `GarminTest.cpp` and `WahooTest.cpp` with their fake
  services and a `wait` that records: 60 files make 2 pauses, 25 none; a
  429 for a file is waited for and retried, and the file comes in; 429s
  past the retries stop the download with the error, with the files
  before it in the inbox. `pause_before()` in `DownloadTest.cpp`.

Not tested automatically: the startup check and the list, which need the
app's job thread and a window. Checked by hand: moving `activity/` away
and starting, picking each period, Esc, and a start with activities.

## Review notes

1. Suggestion: `src/App/SweatTrails.cpp` `notif_store_checked` (~1995). The quiet download is now queued when the UI handles `StoreChecked`, so it lands behind any `GroupRecordings` chunk that was rescheduled after FindNewest (`group_recordings_chunk`, ~305). On an old store with many months to group, the startup download waits for the whole once-over, where before it ran after the first chunk. Harmless, and it only happens once (the marker), but it differs from the spec's "about the same point".
   **Resolved:** accepted, as it happens once per store, and only for a store from before spec 31. The Architecture section and the comment in `initialize()` say so (6d3c79e).
2. Suggestion: `start_download` (~671) lets any quiet request run under a modal. Only `notif_store_checked` submits `quiet` today, so nothing else changes; a future quiet caller inherits the exemption. The comment says so.
   **Resolved:** kept: `quiet` means the startup's download, and the comment in `start_download()` says why it may run under a modal.
3. Suggestion: the `initialize()` comment (~2403) has a line that runs far past the others; rewrap it.
   **Resolved:** rewrapped in 6d3c79e.
4. Checked, no problem: `run_downloads` always setting `login_arguments` (`notif_download_done` passes the object to either login; `cmd_wahoo_login` and `cmd_strava_login` read only `days`/`history`; Garmin never asks a login). Days times 86400 are done in `i64` everywhere, so "Everything" (13464 days) doesn't overflow. `download_periods` is right across both daylight saving changes, the leap year's December 31 and a zone ahead of UTC.
5. Missing test, added: DST changes, leap year, Auckland for `download_periods()`, and the login arguments feeding `make_download_request()` (`test/DownloadTest.cpp`). All pass.
6. Not covered, needs a window: the startup list, Esc, and the "No activities found" box followed by the quiet download.

Review of 9f6870b (pacing):

7. Suggestion: `src/App/storage/Garmin.cpp:146-167`, `SweatTrails.cpp:111`. The Garmin token is obtained once before the download. A paced "Everything" now takes an hour and a half or more, so if the token lives shorter than that, a 401 in the middle stops the download with "Garmin Connect rejected the API key: it may have expired", which sends the user to create a new key for no reason. Nothing is lost (rerun skips what's in the store), and the token's lifetime is unknown; worth checking by hand with a long run, and if short, getting a new token on a 401 once.
   **Resolved:** checked on 2026-11-11 by decoding a fresh token: it's a JWT that lives 24 hours (`exp - iat` = 1440 minutes), far longer than any paced download. No change.
8. Suggestion: `SweatTrails.cpp:91-97`, `Pacing.h:46-53`. After a 429 retry's pause the status line keeps saying "Pausing a minute between downloads" through the retried request, and if that is the last file, until DownloadDone clears it. The next file's progress call restores it otherwise. Cosmetic. The message also says "a minute" whatever `Pacing::pause` is; fine while it is 60 s.
   **Resolved:** accepted as it is: the retry of the last file takes a request's time, and DownloadDone clears the status line right after.
9. Suggestion: `Pacing.h:30`. `pause_before()` counts files in the list, including ones skipped (403/404), so a skipped file counts toward the 25 though it cost a request, which is what's wanted. Retries don't count toward the batch; after a 429 retry the next batch pause still comes at the same index. Fine as spec'd.
10. Checked, no problem: `fetch_paced()` compiles for both `GarminError` and `WahooError` because both have `status`; a network failure has status 0 and isn't retried. Garmin's `get()` turns only 401 into `rejected()` (`is_rejection`), so a 429 reaches `fetch_paced()` as `Failed` with status 429, from the token endpoint as well (no retry there; the list likewise). A 429 for an activity list stops with "Garmin Connect answered HTTP 429 for ..." (Wahoo: "Wahoo's limit on requests is reached. Try again ..."), as the spec says. `notify_latest` replaces a queued Downloading, and the next file's progress call sets the count again after a batch pause, so the status line is right after a pause. The pause blocks the one job thread, as a request does; `AppBus::start` runs it as a plain `std::thread` that is never joined, so quitting doesn't wait for the sleep.
11. Missing test, added: `GarminTest.cpp` `test_too_many_requests_for_the_list_is_not_retried` (a 429 for the list stops at once, no wait, not a key rejection) and `test_retries_are_per_file` (the retry count restarts for each file). Both pass.
12. Found by Jan, downloading his whole Garmin Connect history (about 4,000 activities): SweatTrails crashed with a SIGSEGV while processing the inbox afterwards, and on every start after. A FIT file had a data message for a local message type with no definition before it, and the FIT reader indexed its definitions with SIZE_MAX. Not this feature's code, but the first time so many old files went through the inbox.
   **Resolved:** `read_data_record()` returns `FITError::UndefinedLocalMessage` (5921f46), tested in `activity_load_test`. Such a file stays in the inbox with a warning, as other broken files do (the TODO in `Storage::process_inbox()`).
13. Found by Jan, after the crash in 12 was fixed: updating the totals aborted on every start with "Assertion failed: (compressed_speed_distance.type == FITBaseType::byte)" (`on_load<record>`, `Profile.cpp`). An older device writes a record's compressed speed and distance (field 8) with another base type; the code also read its third byte when the reader had cut it short at a 0xFF.
   **Resolved:** decoded only when it's three bytes of type byte (51c8b40), tested in `activity_load_test` with a deep load. Left as it is: the decoding itself, which loses the distance's top 4 bits (`& 0x000F << 8` lacks parentheses) and takes the 12-bit rolling distance as the distance itself. That's a matter for old devices' files, for a spec of its own if it matters.
14. Found by Jan: "Everything" from Garmin Connect stopped after some 50 activities on every run, and went on to Wahoo. With `TRACE=Garmin` and Jan's history: the next activity, 1020820389 from 2016, is an uploaded TCX file, and a download without a FIT file was an error that ended the Garmin download. Garmin Connect's limit (100 requests a minute, measured: a 429 says `"limit":100,"retryInSeconds":17`) wasn't the cause; the pacing stays under it. The same runs downloaded 111 activities again each time, which the inbox set aside as duplicates, as their start time on Garmin Connect isn't the store's.
   **Resolved:** a download without a FIT file is `GarminError::Kind::NotFit`, skipped as a 403 or 404 is, and activities set aside in `inbox/duplicates/` as `garmin-<id>[-<n>].fit` aren't downloaded again (the commit after 43cb94b's FIT reader fixes), tested in `garmin_test`. Also found: the FIT reader read the older 12-byte header as 14 bytes, which left 850 rides from 2016 and before in the inbox (e26c8b8).
15. Found by Jan's further runs, each fixed with a test that fails against the code before it:
    - 385 activities stayed in the inbox: their sessions and laps have no timer time (field 8), which was required. It's optional, and falls back to the elapsed time (b497942).
    - Garmin Connect answers HTTP 500 for older activities without a file, which stopped the download. Any HTTP error for one activity but 401 and 429 is skipped (fc40fcb), and the ones without a FIT file for good are remembered (see Architecture 8).
    - The FIT reader printed its diagnostics for every file; they're traces in the FIT category (c0e767a).
    - Strava timed out on a page of a whole history's list, and the message box with the error was wider than the window: Strava retries (e287c03), and list boxes wrap their prompt (c914f71).
16. Not resolved: one test program failed intermittently, twice in some twenty runs of the suite, and its name wasn't caught. Jan suspects the Wahoo connection; the tests use fake services, so it's more likely the Wahoo login test's local server on port 8090.
