# Living with Wahoo's and Strava's rate limits

Status: In review, on branch feature/44-rate-limits

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

A download that runs into Wahoo's or Strava's limit on requests carries on
by itself when the limit allows, instead of stopping with a message that
tells the user to try again later. In particular, a whole history's
Strava descriptions, thousands of requests that Strava's daily limit
spreads over days, come in without the user having to keep asking for
them. Builds on [43](43-download-history.md), which paces Garmin Connect
and Wahoo FIT files, and on Strava's titles (`storage/StravaTitles.cpp`).

## Feature

### What there is now

- **Strava.** The titles come with Strava's list, 200 activities a
  request. Each description is a request of its own, the newest first.
  At the first HTTP 429 the descriptions stop, and the closing message
  says "4644 descriptions are left for after 20:00". Strava's read limit
  is per 15 minutes and per day; `rate_limit_reset()` (`Strava.cpp`)
  works out which one was reached from `X-ReadRateLimit-Usage` and
  `-Limit`, and the reset time from that.
- **The backlog is only picked up by a whole history's run.** Ctrl+G
  asks for the titles of the last `strava.days` days, so it only fetches
  those days' descriptions. "Left for after 20:00" really means "until
  you run Get all the titles from Strava (Ctrl+Shift+G) or Everything
  again", and each such run first lists the whole history again, some 30
  requests for Jan's.
- **What the backlog is can be read from the store:** an activity whose
  sidecar has a Strava title (`StravaTitle`, with Strava's ID) but no
  description (`description` is empty as an `std::optional`) is waiting
  for one. Fetching it needs no list.
- **Wahoo.** A 429 for the workout list stops Wahoo's download, with
  "Wahoo's limit on requests is reached. Try again in 4 minutes." (from
  `X-RateLimit-Reset`). A 429 for a FIT file is waited for a minute and
  retried up to 3 times (spec 43). Wahoo's list has no date filter, so
  "Everything" lists the whole history, 30 workouts a request, every
  time.
- **One job thread.** Downloads, the inbox and Strava's titles run on
  the app's one job thread, in order. A wait on it, such as the minute's
  pause between FIT files, holds up everything behind it. A wait of a
  quarter of an hour, or until tomorrow, can't be done that way.

### Strava's descriptions catch up by themselves

- While there are activities in the store waiting for a description
  (a Strava title without one), the app fetches them in the background,
  the newest first, as Strava's limit allows. It needs no list: the
  store has Strava's IDs.
- **It leaves room for the user.** It stops 20 requests short of the
  15-minute limit, so that Ctrl+G and the startup download still work,
  and carries on after the next quarter hour. At the daily limit, it
  carries on the next day, when Strava's day starts (midnight UTC).
  Strava's `X-ReadRateLimit-Usage` and `-Limit` headers say how many
  requests are used and allowed; each request's answer has them.
- **At startup it carries on** where it left off, after the startup
  download: the store says what's waiting.
- **While a batch comes in, the status line says so**, "Strava
  descriptions: 4644 to go", and is clear again between batches.
  Nothing else shows it: no message box when a batch ends. A description
  that comes in shows on the activity screen as any other does.
- A run of Strava's titles (Ctrl+G, Ctrl+Shift+G, Everything) no longer
  fetches descriptions itself: it leaves them to the background, which
  starts right after it. Its closing message says what's coming:
  "Strava: 154 new titles. 4644 descriptions follow in the background."
- The background never waits on the job thread: a wait of a quarter of
  an hour, or until tomorrow, would hold up everything else. When to
  carry on is the Developer's to work out.
- **It's on by default.** The setting `strava.descriptions`, `false`,
  turns it off: then descriptions are fetched by no one, and the
  closing message doesn't mention them.
- **A 429 for the list of a run of Strava's titles**, when it's the
  15-minute limit, is waited out on the job thread, with the status line
  saying "Waiting for Strava's limit until 20:15", and the list carries
  on. With the background's 20 requests to spare it shouldn't happen,
  unless something else uses the same Strava app. The daily limit stops
  the run, as now, with the time it resets in the message.

### Wahoo: a short wait is waited out

- With Wahoo, only the list of workouts counts against the limit: the
  FIT files come from Wahoo's CDN, without a token. A whole history of
  500 to 1000 workouts is 17 to 34 list requests, 30 workouts each.
- SweatTrails is registered with Wahoo as a sandbox app. Wahoo's limits
  aren't in its public documentation; a third-party page gives 25, 100
  and 250 requests for sandbox apps, probably per 5 minutes, hour and
  day. So "Everything" from Wahoo runs into the 5-minute limit once or
  twice, which is what Jan saw: "Try again in 4 minutes."
- A 429 for Wahoo's list waits for `X-RateLimit-Reset` and asks again,
  with the status line saying "Waiting for Wahoo's limit until 20:04",
  as long as the reset is at most 5 minutes away, Wahoo's shortest
  window. That holds up the jobs behind it, such as Ctrl+G's Strava
  titles, for up to 5 minutes, as spec 43's minute's pause between FIT
  files does. A longer one stops Wahoo's download as now, with the time
  in the message, "Try again after 21:00", rather than "in 47 minutes".
- A 429 for a FIT file stays as spec 43 has it: a minute's wait, up to
  3 times.

### Fewer requests

- **Strava's account export** (Settings, My Account, Download or Delete
  Your Account) has an `activities.csv` with every activity's
  description. Importing it brings the whole backlog in without a single
  request. It's [45](45-strava-export-import.md), a spec of its own, and
  can be done before this one: with the backlog in, the background only
  deals with new rides.
- **Wahoo's "Everything" stopping where the store is complete**
  *(deferred)*: remembering how far back Wahoo's history is in, so that
  "Everything" lists only to there, saves some 30 requests a run. With
  short waits waited out, a run costs a few minutes' wait at most, and
  "Everything" is rarely run once the history is in.

### Version 1 scope

In: Strava's descriptions in the background; waiting out Wahoo's short
resets; the closing messages. Deferred: Wahoo's "Everything" stopping
where the store is complete *(deferred)*. The Strava export import is
[45](45-strava-export-import.md).

## Open questions

None.

## Architecture

### What's there

- `Strava::get()` sees every answer's headers, but only a 429 reads
  `X-ReadRateLimit-Usage` and `-Limit` (`rate_limit_reset()`).
- `update_strava_titles()` lists, saves titles, and then fetches the
  descriptions of the run's window in one job. `import_descriptions()`
  (spec 45) already finds the backlog: a sidecar with a `StravaTitle` and
  no description, by walking `months_with_activities()`.
- Jobs that take long already come in chunks that schedule the next
  (`UpdateSummaries`, `GroupRecordings`), with their progress in the
  payload. The UI thread runs `process_input()` every frame.
- `Wahoo::list_workouts()` stops at the first error; `http_error()`
  turns a 429 into "Try again in 4 minutes" from `X-RateLimit-Reset`
  (seconds).

### Decisions

1. **Strava remembers its usage.** In `Strava.h`:

   ```cpp
   // Strava's read limits and how much of them is used, from the
   // X-ReadRateLimit-Limit and -Usage headers: per 15 minutes and per day.
   struct StravaUsage {
       i64 quarter_used { 0 };
       i64 quarter_limit { 0 };
       i64 day_used { 0 };
       i64 day_limit { 0 };
   };

   std::optional<StravaUsage> rate_limit_usage(HttpResponse const &response);
   ```

   `Strava` gets `std::optional<StravaUsage> usage`, which `get()` sets
   from every answer that has the headers, 429s included.
   `rate_limit_reset()` keeps its job, built on `rate_limit_usage()`.
   `StravaError` gets `bool daily { false }` for a 429 at the daily limit.

2. **The background is chunks on the job thread, timed by the UI
   thread.** Nothing sleeps:
   - New `storage/StravaBacklog.{h,cpp}`:

     ```cpp
     // An activity waiting for its description: the Strava ID, and the
     // activities in the store with that ID (the same ride on two devices
     // takes one request).
     struct Undescribed {
         u64                      id { 0 };
         std::vector<StoredStart> stored { };
     };

     // The store's activities with a Strava title and no description,
     // newest first.
     StorageResult<std::vector<Undescribed>> strava_backlog(Storage const &storage);

     // How many requests the background leaves the user, of each limit.
     constexpr i64 StravaSpareRequests = 20;

     // When the background may ask again, by `usage`: now (nullopt), at
     // the next quarter hour, or at midnight UTC.
     std::optional<i64> backlog_resume_at(std::optional<StravaUsage> const &usage, i64 now);

     struct BacklogChunk {
         size_t             fetched { 0 };
         std::optional<i64> resume_at { }; // Strava's limit, or a failure: when to go on
         bool               stopped { false }; // Not logged in: until a titles run
         std::vector<i64>   changed { };       // By start time
     };

     // Fetches up to `count` descriptions from the front of `waiting`, and
     // removes the ones it saved. Stops early when backlog_resume_at() says
     // so, at a 429 or any other error.
     BacklogChunk fetch_backlog(Strava &strava, std::vector<Undescribed> &waiting, size_t count);
     ```

     `import_descriptions()` keeps its own walk: it also counts the
     activities without a Strava title, and sharing would bring
     `Strava.cpp` into the export's test. An activity Strava answers 404
     for gets an empty description, so that it isn't asked for at every
     batch. Before saving, `fetch_backlog()` reads the sidecar's Strava title
     again and saves only if it's still the same ID without a
     description, so a name a titles run saved in between stays.
   - A job `DescribeFromStrava` with payload `DescribingProgress
     { StravaClient client; std::optional<std::vector<Undescribed>>
     waiting; std::optional<StravaUsage> usage; }`:
     the first chunk scans the store, each chunk fetches 10
     descriptions (a few seconds) and schedules the next while Strava
     allows, so Ctrl+G's job waits for at most one chunk. It notifies
     `Describing { size_t to_go; }` (`notify_latest`, the status line)
     before each chunk and `DescribingPaused { std::optional<i64>
     resume_at; }` when the chain ends: backlog empty, a limit, a failure
     (resumes at the next quarter hour), not logged in, or not set up.
     The background's `Strava` has no retries: those would wait on the
     job thread, and a failure goes on at the next quarter hour anyway.
   - On the UI thread, `SweatTrails` keeps `describing` (a chain is
     queued or running) and `describe_at` (`std::optional<i64>`).
     `process_input()` schedules the job when `describe_at` has come and
     nothing is describing. `DescribingPaused` clears the status line and
     sets `describe_at` to `resume_at`.
   - **What starts it:** every `DownloadDone` of a run that asked Strava
     for titles (Ctrl+G, Ctrl+Shift+G, Everything, the startup's quiet
     one, a Strava login's). `DownloadReport` gets `bool describe`. If a
     chain is running then, it's flagged to start again right away when
     it ends with an empty backlog, so new titles are picked up.

3. **A titles run no longer fetches descriptions.**
   `update_strava_titles()` drops its description loop and `progress`;
   `StravaUpdate` drops `descriptions`, `resets_at` and `waiting`.
   `download_from_strava()` counts `strava_backlog()` after the titles
   (by Strava ID, as the status line does) into `SourceOutcome::waiting`;
   `SourceOutcome::descriptions` and `resets_at` go. The sentence is
   "Strava: 154 new titles. 4644 descriptions follow in the background."
   With `strava.descriptions` false (`strava_descriptions(settings)` in
   `Download.h`, default true), nothing counts, mentions or starts them.

4. **Waiting out the 15-minute limit for Strava's list.** `Strava` gets
   `std::function<void(i64 until)> wait_for_limit`. `list_page()`, on a
   429 that isn't `daily`, calls it and asks again, at most twice per
   page; without it, it stops as now (the tests' default). The app's
   says "Waiting for Strava's limit until 20:15" and sleeps on the job
   thread. `description()` never waits.

5. **Waiting out Wahoo's short resets.** `Wahoo` gets the same
   `wait_for_limit`, and `WahooMaxLimitWait = 300` seconds.
   `list_workouts()` on a 429 with `X-RateLimit-Reset` at most 300 asks
   for the same page again after the wait, at most twice per page. A
   longer reset says "Wahoo's limit on requests is reached. Try again
   after 21:00.", rounded up to the minute. `WahooError` gets
   `resets_at`, and `Wahoo.cpp` makes the message with `clock_time()`,
   now in Base.
   FIT files stay as spec 43 has them.

6. **Status texts.** `DownloadStep::Waiting`, and
   `download_status()` takes an `until` text: "Waiting for Wahoo's limit
   until 20:04". `describing_status(to_go)`: "Strava descriptions: 4644
   to go". `clock_time(i64)`, now in `SweatTrails.cpp`, moves to
   `Date.h` in Base, so that `Wahoo.cpp`, `StravaTitles.cpp` and tests
   can use it. Strava's `next_quarter_hour()` and `next_strava_day()`
   are in `Strava.h`, for `backlog_resume_at()`.

7. **The 20 spare are left on both limits.** The spec says the
   15-minute one; leaving 20 of the day too keeps Ctrl+G working in the
   evening after a day of background requests. Agreed with Jan.

Nothing new in `src/Lib/Widget`: the background shows only on the
status line.

## Test plan

- `StravaTest.cpp`: `rate_limit_usage()` with and without headers and
  with bad ones; `get()` keeps `usage` from a 200 and a 429; a 429 at
  the 15-minute limit for the list is waited for (a recording
  `wait_for_limit`) and the list carries on; a third one stops it; the
  daily limit stops without waiting; no `wait_for_limit`, no wait.
- `StravaBacklogTest.cpp` (new, with `TempStore` and `FakeStrava`):
  `strava_backlog()` finds titled, undescribed activities newest first,
  two activities with one ID once, skips described and untitled ones;
  `backlog_resume_at()` with no usage, under, at 80 of 100, the daily
  limit at 980 of 1000, across midnight UTC; `fetch_backlog()` fetches
  `count`, stops at the spare from the headers with the next quarter
  hour, at a 429 with its reset, at a failure, not logged in; a 404 gets
  an empty description; saves both activities of one ID; leaves a name
  and a description saved in between.
- `StravaTitlesTest.cpp`: the description tests go; a run saves titles
  and makes no description request; a new name drops the description;
  the day's limit on the list's second page says when it resets.
- `WahooTest.cpp`: a 429 with reset 250 waits and the list carries on;
  two in a row still carry on, a third stops; reset 301 stops with
  `resets_at`; no header stops as now.
- `DownloadTest.cpp`: the Titled sentence with descriptions following;
  `strava_descriptions()` and the request's; `download_status()` for
  `Waiting`; `describing_status()`.
- By hand: Jan's backlog with the export not imported, watching the
  status line and the log across a quarter hour; Ctrl+G during a chain;
  `strava.descriptions: false`; a restart mid-backlog.

## Review notes

1. **Suggestion.** `describe_from_strava()` (SweatTrails.cpp) starts a chain
   with `usage` empty, also from `notif_download_done` while `describe_at`
   waits for a limit. Every Ctrl+G during a pause so costs one request out of
   the 20 spare before the chain sees the headers and stops again. Keeping
   the last `usage` in `SweatTrails`, or not starting while `describe_at` is
   in the future, avoids it. Harmless otherwise.
2. **Suggestion.** `waiting_for_limit()` sleeps on the job thread for up to
   `StravaLimitWaits` x 15 minutes per list page (and 2 x 5 for Wahoo), also
   in the quiet startup run, which holds up the inbox and Ctrl+G behind it
   with nothing but the status line to say why. The spec accepts this; it is
   only reachable when something else uses Strava's quota.
3. **Suggestion.** `Strava.cpp` logs `resets_at` in the "waiting until"
   warning as a raw Unix time; `clock_time()` would read better.
4. **Missing tests, added** (`test/StravaBacklogTest.cpp`, all pass): a 429
   without headers resumes at the next quarter hour and does not stop the
   chain or leave `resume_at` empty (which would make the chain hammer
   Strava); usage 79/979 carried in still fetches; a 404 for an ID on two
   devices empties both; a sidecar that got another Strava ID in between
   isn't given the old ID's description.
5. **Not tested** (needs the app's job thread, as the Developer says): the
   `describing`, `describe_at`, `describe_again` chain. Reading it, no way to
   get stuck was found: every path of `describe_chunk()` either schedules the
   next chunk or sends `DescribingPaused`; `describing` is only set in
   `describe_from_strava()` and cleared by that notification; a login or
   titles run ending mid-chain sets `describe_again`, and a restart loop
   ends since `describe_again` is cleared each time; a failed or limited
   chunk sets `describe_at`, which `process_input()` picks up.

