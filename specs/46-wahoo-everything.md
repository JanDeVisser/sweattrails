# Wahoo's "Everything" stops where the store is complete

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

Once Wahoo's whole history is in the store, "Everything" from Wahoo costs
one or two list requests instead of the whole history's 72, so that
running it again, or many times in a night, doesn't use up Wahoo's daily
limit. And when a limit is used up anyway, the quiet download at startup
says so on the status line, instead of in a message box at every start
until it resets.

Follows [44](44-rate-limits.md), which deferred "Everything stopping
where the store is complete", and its review note 6, and
[43](43-download-history.md), which added "Everything".

## Feature

### What there is now

- **Wahoo's limits**, measured from the `X-RateLimit-*` headers on
  2026-11-13 (`TRACE=Wahoo` logs them on every answer), for SweatTrails'
  sandbox app: 250 requests a day, 100 an hour, 25 per 5 minutes.
  `x-ratelimit-remaining` lists the same three, in that order.
  `x-ratelimit-reset` is 0 while under the limits; at a 429 it's the
  seconds until the limit that was hit resets. The day resets at
  midnight UTC (19:00 in Toronto in winter). Wahoo doesn't document any
  of this.
- **Only the list and the tokens count.** The list is `/v1/workouts`, 30
  workouts a page, newest first, without a date filter. FIT files come
  from `cdn.wahooligan.com` without a token, and don't count.
- **What a run costs.** The startup download and Ctrl+G list the last
  `wahoo.days` days: one request. "Everything" lists to the end of the
  list: a page per 30 workouts, 72 for Jan's 2143 (Wahoo's `total` on
  2026-11-13; 543 of them came into the store, the others it had from
  Garmin Connect, or they have no FIT file), every time, however much of
  it the store has. On 2026-11-11 "Everything" was started many
  times in an evening, and used up the day's 250.
- **A limit that resets in more than 5 minutes stops the download**
  (spec 44), with "Wahoo's limit on requests is reached. Try again after
  19:00." The quiet download at startup reports failures in a message
  box, so with the day's limit used up, every start shows that box until
  19:00.

### "Everything" lists only what the store may not have

- **The data folder remembers how far Wahoo's history is complete**: a
  point in time, T, up to which the store has everything Wahoo had. It's
  a small file of its own next to `garmin-unavailable.txt` (spec 43);
  its name and format are the Developer's.
- **"Everything" lists back to T, less `wahoo.days`**, instead of to the
  end of the list. The overlap catches a ride recorded before T but
  synced to Wahoo later. Without the file, "Everything" lists the whole
  history, as now.
- **A complete run sets T** to the time it started. Complete means it
  listed as far as it meant to (the end of the list, or its `since`),
  and downloaded every workout it wanted without an error. Workouts
  skipped as unavailable (403 or 404) and ones without a FIT file count
  as done.
  - An "Everything" without the file that completes sets T.
  - Any other run, the startup download, Ctrl+G or a period from
    `st-download-period`, moves T forward only if it looked back at
    least as far as T, so that there's no gap. As the app is started
    most days, T stays close to today, and "Everything" costs what
    Ctrl+G does: one or two pages.
  - After more than `wahoo.days` without a start, the startup's run
    doesn't reach T, and T stays. The next "Everything" lists back to T
    and fills the gap, and moves T.
- **A run that doesn't complete leaves T as it was.** An "Everything"
  interrupted on the way through a whole history (quitting, an error, a
  limit) lists the whole history again next time. That's 72 pages for
  Jan's, three waits for the 5-minute limit, and needs an hour with most
  of its 100 left: a first run is best started after the day resets. It
  only happens until the first one completes. If interrupted first runs
  turn out to hurt, a listing that resumes where it stopped is a spec of
  its own.
- **Deleting the file lists the whole history again**, as deleting
  `garmin-unavailable.txt` asks Garmin Connect again.
- **What "Everything" no longer brings back:** an activity deleted from
  the store, and a ride synced to Wahoo more than `wahoo.days` after T.
  Before, "Everything" brought those in. Deleting the file does now.
  Agreed with Jan.
- **Why not stop at the first page the store has all of**, which needs
  nothing remembered: downloads go newest first, so an interrupted run
  leaves the newest pages complete and the older ones not, and the next
  run would stop at page 1 and never get the rest. And some workouts
  never reach the store (no FIT file, unavailable, duplicates the inbox
  sets aside), so "the store has all of the page" can't be relied on.

### A limit at startup goes on the status line

- **A limit that stops the quiet download at startup goes on the status
  line**, not in a message box: a 429 that isn't waited out. That's
  Wahoo's daily or hourly limit (a reset more than 5 minutes away), and
  Strava's daily limit for its list of titles, which the quiet run
  reports as a failure now too. The text is the message as now, such as
  "Wahoo's limit on requests is reached. Try again after 19:00."
- **It isn't sticky**: it stays until something else is put on the
  status line, such as Strava's descriptions in the background, as any
  status text does. The log has it too. A status line that holds a text
  against others would cost more than it's worth.
- **Other failures at startup stay in a message box**, as now: no
  connection, a login Wahoo no longer accepts, and so on.
- **Downloads the user starts are as they are**: Ctrl+G, a period, or
  "Everything" while a limit is used up asks the service, and a limit
  stops it with the message box, as now. The user asked, so gets a clear
  answer, and a 429 costs nothing against the limit. The app doesn't
  remember when a limit resets to skip asking.
- When the startup's download stops at a limit, what it downloaded
  before it, from Garmin Connect for one, is processed as now.

### Not done

- **Budgeting by what Wahoo says is left** (`x-ratelimit-remaining`),
  suggested in spec 44's review note 6: with T, "Everything" costs one
  or two requests, and the one expensive run, the first whole history,
  is 72 pages and a token request, under the hourly limit of 100.
  There's nothing left to budget.

### Version 1 scope

Everything above: "Everything" from Wahoo listing back to where the store
is complete, the file that remembers it, and a limit at startup on the
status line. Garmin Connect's "Everything" is unchanged: its list takes a
start date, and its limit hasn't been a problem.

## Open questions

None.

## Architecture

### What's there

- `download_new_workouts()` (`storage/Wahoo.cpp`) lists back to a
  `since`, and returns an error only for what stops it: a failed list
  page, a FIT file that fails other than 403 or 404, a file it can't
  write. Workouts without a FIT file, unavailable ones and ones already
  in the store or the inbox are passed over. So a run that returns a
  count is complete, as the spec means it.
- `download_from_wahoo()` (`SweatTrails.cpp`) turns `days` into `since`.
  "Everything" is `days` back to January 1, 1990 (`EverythingSinceYear`),
  and the request's `wahoo_days` then is that, not the setting's.
- `download_report()` skips everything but `Failed` when quiet;
  `notif_download_done()` shows the text in a message box.
- Strava's list at its limit is `StravaError::Kind::RateLimited`, which
  `download_from_strava()` makes a `Failed` outcome; a limit on a later
  page is a `Titled` outcome with a message, which quiet doesn't show.

### Decisions

1. **"Everything" is recognized by how far it looks back**, not by a
   flag: in `storage/Wahoo.h`,

   ```cpp
   // Whether a download that looks back to `since`, a Unix time, asks
   // for Wahoo's whole history, as "Everything" does: since before
   // EverythingSinceYear, give or take a time zone.
   bool whole_history(i64 since);
   ```

   So nothing between the period list and Wahoo changes, and the Wahoo
   login after an "Everything" (which passes on `days`) is one too.
   `whole_history()` is true for `since` before January 2 of
   `EverythingSinceYear`, UTC.

2. **The file** is `wahoo-complete.txt` in the data folder, next to
   `garmin-unavailable.txt`: one line, T as ISO 8601 in UTC, such as
   `2026-11-13T18:04:05Z`, so that it reads as a time when looked at. A
   file that's missing or can't be read is no T (with a warning when it's
   there and unreadable): the whole history again, which is safe.

   ```cpp
   fs::path           wahoo_complete_file(Storage const &storage);
   std::optional<i64> read_wahoo_complete(fs::path const &file);
   WahooResult<void>  write_wahoo_complete(fs::path const &file, i64 complete);
   ```

   Written with `write_file_atomically()`. Formatted with
   `std::format("{:%FT%TZ}", sys_seconds)`, read with
   `DateTime::parse_iso8601()`, as Wahoo's start times are.

3. **What a run lists, and whether it moves T**, is a pure function:

   ```cpp
   // What a download from Wahoo lists, by how far the store is complete:
   // see specs/46-wahoo-everything.md.
   struct WahooPlan {
       i64  since { 0 };         // Lists back to here
       bool completes { false }; // Done without an error, the store is complete up to its start
   };

   // For a download that looks back to `since`, with the store complete
   // up to `complete`. "Everything" lists back to `complete` less
   // `overlap` seconds, or the whole history without it.
   WahooPlan plan_wahoo_download(i64 since, std::optional<i64> complete, i64 overlap);
   ```

   - whole history, T: since T less overlap; completes.
   - whole history, no T: since as asked; completes.
   - otherwise, T: since as asked; completes if since <= T.
   - otherwise, no T: since as asked; doesn't complete.

4. **One function does the run and the file**, so that a test reaches
   all of it with the fake Wahoo and a `TempStore`:

   ```cpp
   // download_new_workouts() for a download that looks back to `since`,
   // as plan_wahoo_download() has it, and, if it completes, T in
   // wahoo_complete_file() set to the time it started. A T that can't be
   // written is a warning: the next "Everything" lists more.
   WahooResult<size_t> download_wahoo_history(Wahoo &wahoo, Storage const &storage, i64 since, i64 overlap,
       WahooProgress const &progress = { }, Pacing const &pacing = { });
   ```

   The start time is `wahoo.now()`, so tests control it.
   `download_from_wahoo()` calls it instead of `download_new_workouts()`.

5. **The overlap is the setting's `wahoo.days`**, which the task's `days`
   overrides in `wahoo_days`. `DownloadRequest` gets
   `int wahoo_overlap_days { WahooDefaultDays }`, always from the settings,
   and `WahooLogin` the same, filled in by `cmd_wahoo_login()`, for the
   download after a login.

6. **A limit at startup.** `SourceOutcome` gets `bool limited { false }`:
   failed at a limit on requests that wasn't waited out. Wahoo sets it
   for an error with status 429, Strava for `RateLimited`. Garmin Connect
   doesn't: the spec is about Wahoo and Strava.
   `DownloadReport` gets `std::string status { }`. Quiet, a limited
   failure goes there instead of in `text`; not quiet, in `text` as now.
   `notif_download_done()` puts it on the status line after clearing it;
   `run_downloads()` logs it as a warning. A message box for another
   source's failure still shows.

Nothing for `src/Lib/Widget`: the status line is there.

## Test plan

- `WahooTest.cpp`:
  - `whole_history()`: "Everything"'s `since` from `download_periods()`
    is, "The last year"'s isn't.
  - `read_wahoo_complete()`/`write_wahoo_complete()`: round trip; no
    file; a file with something else in it.
  - `plan_wahoo_download()`: the four cases; a run before T, at T and
    after T.
  - `download_wahoo_history()` with `TempStore` and the fake Wahoo:
    - "Everything" without the file lists every page and writes T as
      the start time.
    - "Everything" with T asks for one page when the first page goes
      back past T less the overlap, and moves T.
    - A 30-day run with T inside its reach moves T; with T before its
      reach, leaves it; without the file, writes none.
    - A run with a failed FIT file, or a limit on page 2, leaves T.
    - A workout without a FIT file and one answered 404 don't keep a
      run from completing.
- `DownloadTest.cpp`: quiet, a limited failure goes to `status`, not
  `text`, and a non-limited one stays in `text`; not quiet, a limited one
  is in `text`; `make_download_request()` keeps the setting's days in
  `wahoo_overlap_days` when `days` is given.
- By hand: "Everything" twice with `TRACE=Wahoo`, the second listing one
  or two pages; the file's contents; deleting it lists all 72 again; a
  start with a used-up limit (or a fake 429) shows it on the status line.

## Review notes

1. Suggestion: `download_from_wahoo()` sets `limited` for any 429, which includes one that was retried and still answered 429 within 5 minutes (`test_too_many_requests_past_the_retries`). The spec says "a 429 that isn't waited out" is a daily or hourly limit; a short-reset 429 at startup now goes on the status line, not a message box. Arguably fine (still a limit), but it's wider than the spec's wording.
   **Resolved:** kept, agreed with Jan: a 5-minute limit still there after the waits, or a FIT file refused past its retries, stops the download at a limit too, and belongs on the status line as much as a daily one.
2. Suggestion: a clock set back past T leaves T in the future, so every run has `since <= T` and "completes", and "Everything" lists back to a T-less-overlap that's too recent. Unlikely; clamping T to `started` on read would cover it.
   **Resolved:** `download_wahoo_history()` takes a T after the run's start as the start, with a test that fails without it (b487028).
3. Suggestion: on a startup that stops at a limit, `describe_from_strava()` in `notif_download_done()` may replace the status text at once. The spec accepts that (not sticky).
   **Resolved:** kept as the spec has it.
4. Missing test (added): "Everything" with T that needs a second page, in `test/WahooTest.cpp`: `test_everything_with_the_file_goes_on_to_the_page_that_passes_it`. Passes.

No bugs found against Version 1 scope. `ctest`: all pass except `oauth_test`, the known flaky one.

5. Found by Jan, trying the first "Everything" by hand: it went past
   page 50, where the spec said 19, and ran into the hourly limit. The
   per-page trace (added for it) showed Wahoo's `total` is 2143, not
   543: the 19 pages were worked out from the workouts that came into
   the store, never measured. The paging is right.
   **Resolved:** the spec's numbers are corrected to 72 pages, with a
   first run needing an hour with most of its 100 requests left. Agreed
   with Jan to keep the design, and to make the listing resumable in a
   spec of its own if interrupted first runs turn out to hurt.
