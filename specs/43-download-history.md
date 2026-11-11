# Download history

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

- "Everything" can be thousands of FIT files, and how long that takes,
  or whether Garmin's Personal API slows it down, is unknown. The status
  line counts, as for any download ("Downloading 412 of 2300 from Garmin
  Connect").
- There's no way to stop a download but quitting. That's enough, as
  nothing is lost: what came in is in the inbox and is processed at the
  next start, and running "Everything" again skips what the store has,
  so it picks up where it stopped for the cost of the list requests. A
  stop command *(deferred)*.

### An empty store

- **At startup, when the store has no activities and at least one source
  is set up**, SweatTrails shows the same list instead of running the
  quiet 30-day download, with a line above it: "There are no activities
  yet. Download from…". Esc closes it and downloads nothing.
- "Empty" is after the inbox is processed at startup: FIT files waiting
  in the inbox, such as what an interrupted download left, count.
- With no source set up, nothing is asked, as now.
- Moving `activity/` away is all it takes to start over: nothing else in
  the data folder has to go. `summaries.json` (the totals) keeps only
  the summaries of FIT files that are in the store
  (`plan_update()`, `storage/Summaries.cpp`), so it empties itself at the
  next start. The `recordings-grouped` marker only says the once-over of
  an older store for recordings (spec 31) is done; activities that come
  in through the inbox are grouped as they come, so it can stay.

### Version 1 scope

In: the `st-download-period` task and its list of four periods, and the
list at startup for an empty store. Deferred: a way to stop a running
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
   now. `run_downloads()` keeps scheduling `FindNewest { }`, not at
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

Nothing new in `src/Lib/Widget`: `list_box()` is the widget.

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

Not tested automatically: the startup check and the list, which need the
app's job thread and a window. Checked by hand: moving `activity/` away
and starting, picking each period, Esc, and a start with activities.

## Review notes

1. Suggestion: `src/App/SweatTrails.cpp` `notif_store_checked` (~1995). The quiet download is now queued when the UI handles `StoreChecked`, so it lands behind any `GroupRecordings` chunk that was rescheduled after FindNewest (`group_recordings_chunk`, ~305). On an old store with many months to group, the startup download waits for the whole once-over, where before it ran after the first chunk. Harmless, and it only happens once (the marker), but it differs from the spec's "about the same point".
2. Suggestion: `start_download` (~671) lets any quiet request run under a modal. Only `notif_store_checked` submits `quiet` today, so nothing else changes; a future quiet caller inherits the exemption. The comment says so.
3. Suggestion: the `initialize()` comment (~2403) has a line that runs far past the others; rewrap it.
4. Checked, no problem: `run_downloads` always setting `login_arguments` (`notif_download_done` passes the object to either login; `cmd_wahoo_login` and `cmd_strava_login` read only `days`/`history`; Garmin never asks a login). Days times 86400 are done in `i64` everywhere, so "Everything" (13464 days) doesn't overflow. `download_periods` is right across both daylight saving changes, the leap year's December 31 and a zone ahead of UTC.
5. Missing test, added: DST changes, leap year, Auckland for `download_periods()`, and the login arguments feeding `make_download_request()` (`test/DownloadTest.cpp`). All pass.
6. Not covered, needs a window: the startup list, Esc, and the "No activities found" box followed by the quiet download.
