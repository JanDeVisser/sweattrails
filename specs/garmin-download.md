# Download Activities from Garmin Connect

Status: In review, on branch feature/garmin-download

## Goal

Activities recorded on Jan's Forerunner 965 show up in SweatTrails without
copying FIT files by hand.

## How it works now

- The app picks up FIT files in the inbox (`<data dir>/inbox`) when it
  starts (`Storage::process_inbox()`, run as the `ProcessInbox` job). It
  moves each into `activity/<year>/<month>/<start time>.fit`, skips
  duplicates by start time, and leaves anything that isn't an activity.
  Whatever brings files into the inbox gets organizing, duplicates and
  analysis for free.
- Until now, activities reached the inbox through a Rungap webhook on a
  server of Jan's (`RUNGAP_API_URL`), which writes a FIT file and a
  `<stem>.json` sidecar for each. The inbox moves the sidecar along with
  its FIT file (`Storage::move_with_sidecar()`), but nothing reads it. The
  sidecars in Jan's store hold only `{"start_time": <timestamp>}`.
- `storage/Strava.*` and `storage/Rungap.*` are translations of the Zig
  version. They aren't built into the app (not in `CMakeLists.txt`).
  Rungap's client fetches activities from the webhook's server with a
  bearer token. Strava's has OAuth, with a local redirect server. Both
  shell out to `curl` through `ST::Process`. `Map.cpp` uses libcurl for
  tiles.

### Garmin's Personal API

Garmin opened its Connect Developer Program to individuals on October 21,
2026, with a free Personal tier: "secure access to your own Garmin Connect
data", for "developers, hobbyists and data enthusiasts", not for commercial
apps that serve other people. That fits SweatTrails: each user creates
their own key, for their own data.

What's known, from Garmin's overview page, Garmin's own Python client
([garmin/connect-personal-api-python](https://github.com/garmin/connect-personal-api-python),
Apache 2.0) and the first reports:

- **The key.** Created on the Garmin Connect website, in the account
  settings, with no application or approval. It's valid for 30, 90 or 180
  days, or a year, and is only shown when it's created. Garmin's client
  looks for it in `GARMIN_PERSONAL_API_KEY`, then in
  `~/.config/garmin/connect-personal-api-key`.
- **The token.** `POST https://personal.connectapi.garmin.com/personalapitoken-service/personal-api/personal-api-key/token`
  with `Authorization: ApiKey <key>` returns a JWT as plain text. Its
  `exp` claim says when it expires (reported as 24 hours). Requests then
  use `Authorization: Bearer <token>`. A revoked or expired token gives a
  401.
- **Listing activities.** `GET /activitylist-service/activities/search/activities`
  with `start` and `limit` for paging (default 20), and optional
  `startDate` and `endDate` (`YYYY-MM-DD`). It returns JSON summaries,
  reported to have the same shape as Garmin Connect's website: an
  `activityId`, the start time, the activity's name, its type, and so on.
- **Downloading.** `GET /download-service/files/activity/{activityId}`
  returns the original file as a ZIP, with the FIT file in it. Garmin
  strips some data it considers proprietary from these files, such as VO2
  max; the activity data itself is there.
- **Read-only, pull-only.** Nothing can be uploaded or changed.
- **Not documented yet:** rate limits, and how far back the history goes
  (first reports say it does go back). Garmin publishes a Swagger document
  for the Personal API.
- The FIT files don't have the name given to an activity on Garmin
  Connect, such as "Westmount-Wallenstein": the activity list does.

The Forerunner 965 connects over USB as an MTP device, not as a disk, so
copying from the watch would need libmtp. With the API there's no need.

## Feature

### Downloading

- SweatTrails downloads new activities from Garmin Connect when it starts,
  before it processes the inbox, and again on Ctrl+G while it runs. The
  downloaded FIT files go into the inbox, and from there the way they
  always have: organized by start time, duplicates skipped.
- The download is a task, `st-garmin-download`, registered with
  `bus.add_task()` like `st-quit` and `st-list-month`, and Ctrl+G is bound
  to it. Jan plans a command palette, as in VS Code and Zed, that lists
  and runs tasks by name, so every action a user can trigger has to be a
  task. The startup download runs the same task, so there's one way in.
  It takes an optional `days` argument that overrides `garmin.days` for
  that one download, so the palette, or a binding, can offer "the last
  year" without changing the settings.
- After a download, the inbox is processed, whether the download was at
  startup or on the task: the files it brought in show up right away.
- Each download looks at the last N days of activities on Garmin Connect
  (`garmin.days` in the settings, 30 by default). An activity whose start
  time SweatTrails already has isn't downloaded, so a download that finds
  nothing new costs one request for the token and one for the list, and an
  activity that arrived some other way, from Rungap or by hand, isn't
  fetched again (see Already downloaded). Older activities can be had by setting a larger N once.
- Activities without a FIT file (entered by hand on Garmin Connect) are
  skipped.
- While downloading, the status line says so, with a count, as for the
  inbox ("Downloading 3 of 12 from Garmin Connect"). When it's done, the
  activity list and the newest activity show what came in, as after the
  inbox.

### The API key

- The key is where Garmin's own client looks for it: the
  `GARMIN_PERSONAL_API_KEY` environment variable, else
  `~/.config/garmin/connect-personal-api-key`. Garmin's command-line tool
  and SweatTrails share it. SweatTrails warns if the file can be read by
  others, as Garmin's client does. It never writes the key.
- The token is fetched for each download and kept in memory only: it's one
  request, and a download is at most a few times a day.

### The sidecar and titles

- For each FIT file it downloads, the download writes the activity's
  summary from Garmin Connect's activity list, as it came, to the inbox as
  `<stem>.json`, the sidecar the inbox already moves along with its FIT
  file. Garmin Connect takes over this name from the Rungap webhook.
- The activity's name on Garmin Connect (`activityName` in the summary)
  becomes its title in SweatTrails: when an activity is loaded, a title
  from the sidecar wins over the FIT file's workout name. A sidecar
  without one, such as the Rungap webhook's, or one that isn't valid
  JSON, is ignored.
- Activities already in the store keep their titles: they aren't
  downloaded again, so they don't get a Garmin sidecar.

### Already downloaded

- An activity SweatTrails already has isn't downloaded again. It's
  recognized by its start time, which the store names its files by.
- That needs Garmin Connect's start time in the activity list to match the
  FIT file's to the second. The Developer checks this first, with a real
  download. If they don't match, the sidecars' `activityId`s tell which
  Garmin activities the store has instead.

### Rungap

- With the Personal API, the Rungap webhook is obsolete. This feature
  removes `storage/Rungap.*`, which isn't built anyway, and the mentions
  of the webhook in the inbox code. Jan turns off the webhook's server.

### Errors

- No key: at startup, nothing happens and nothing is said; Garmin
  download is something a user opts into by creating a key. On Ctrl+G, a
  message says how to create one and where to put it.
- The key is rejected (expired after its 30 days to a year, or revoked):
  a message says so, and that a new one is made on the Garmin Connect
  website. The inbox is processed anyway.
- Garmin Connect can't be reached, or a request fails: a message with the
  error. Activities downloaded before the failure stay in the inbox and
  are processed; the next download picks up the rest.

### Version 1 scope

Everything above. Deferred: the command palette, and the human-readable
names for tasks that it needs, are a spec of their own *(deferred)*;
health data, which the Personal API also has
*(deferred)*; and downloading on a timer while the app runs *(deferred)*.

## Open questions

None.

## Architecture

**Checked first, with a real download (October 23).** The key gives a
token valid for 24 hours. The list's `beginTimestamp` (milliseconds since
1970) divided by 1000 is the FIT session's `start_time` to the second, and
so the store's file name: three activities that came in through Rungap
match. The download is a ZIP with one file, `<activityId>_ACTIVITY.fit`.
Garmin's FIT file is a different file from the one Rungap delivered (399
KB against 91 KB for a Zwift ride), with the same start time. So
recognizing activities by start time works, and the `activityId` fallback
isn't needed.

**HTTP.** A small libcurl wrapper, `src/App/Http.h`, for requests that
return their body in memory:

```cpp
struct HttpRequest {
    std::string              method { "GET" };
    std::string              url;
    std::vector<std::string> headers;
};

struct HttpResponse {
    long        status { 0 };
    std::string body;
};

using HttpFetch = std::function<std::expected<HttpResponse, LibCError>(HttpRequest const &)>;

std::expected<HttpResponse, LibCError> http_fetch(HttpRequest const &request);
```

The Garmin code takes an `HttpFetch`, so tests give it canned responses
and never touch the network. `Map.cpp`'s tile download stays as it is: it
streams to a file.

**Unzipping.** A ZIP reader in `src/Lib/Base/Zip.h`, on zlib, which macOS
and Linux have (and libcurl uses already): it finds an entry in a ZIP held
in memory, and returns its contents, stored or deflated. Only what Garmin's
ZIPs need: no ZIP64, encryption or multi-disk. A new dependency, zlib, but
no new library to install. The other options were shelling out to
`unzip`, or libarchive or minizip from Homebrew.

**Garmin, in `src/App/storage/Garmin.h`:**

- `read_api_key(environment value, file)`: the key from
  `GARMIN_PERSONAL_API_KEY`, else the file, trimmed; nothing if neither
  has one. An empty variable counts as none. Warns if the file can be read by group or others.
- `struct GarminActivity { u64 id; i64 start_time; bool has_file; JSONValue
  summary; }`, and `parse_activity_list(JSONValue)`: the list's entries.
  `start_time` is `beginTimestamp / 1000`; `has_file` is false for manual
  activities.
- `struct GarminConnect`, holding the `HttpFetch` and the token:
  `obtain_token(key)`, `list_activities(since)` (pages of 100 until a short
  page), `download_fit(id)` (the ZIP, and the `.fit` in it).
- `download_new_activities(GarminConnect &, Storage const &, days,
  progress)`: lists the last `days` days, skips what the store has, and
  writes each new activity to the inbox as `garmin-<activityId>.json` (the
  summary) and then `garmin-<activityId>.fit`, each through a `.part` file
  and a rename, so the inbox never sees half a file. The sidecar goes
  first, so the inbox can't take a FIT file without it. The inbox renames
  both by start time, as now.
- `GarminError`: no key, key rejected (401 or 403 on the token), an HTTP
  error with its status, and the `LibCError` and `JSONError` underneath.

**The store.** `Storage::has_activity(i64 start_time)`: whether
`activity/<year>/<month>/<start_time>.fit` exists, with the year and month
worked out as `organize_fit_file()` does.

**Titles.** `Activity::load_from_dir()` reads `<name>.json` if there is
one, and sets `title` to its `activityName` if it has one, after the FIT
file, so it wins over the workout name. A sidecar without it, or that
isn't JSON, is ignored, with a trace.

**JSON numbers.** Found running the download against Garmin: the JSON
decoder in `src/Lib/Base` read neither negative numbers nor exponents,
because it took its numbers from the general lexer. Every longitude in
Canada is negative. `JSON.cpp` now scans JSON numbers itself, through the
lexer's `pre_match()` hook, so the lexer is unchanged for its other uses,
and converts them with `std::from_chars`. Converting a negative integer to
a signed type failed too, and is fixed.

**Reading the sidecar** doesn't use `JSONValue::read_file()`, which ends
the program on a syntax error: a damaged sidecar would have taken the app
down.

**The task and the job.** `st-garmin-download` is added with
`bus.add_task()` in `SweatTrails::initialize()`, and bound to Ctrl+G. Its
handler schedules three jobs, which run in order: a new
`DownloadGarmin` job, `ProcessInbox` and `FindNewestActivity`. The
arguments are `{"days": N}`, optional, and `{"quiet": true}`, which
startup passes, to say nothing when there's no key. At startup,
`initialize()` submits the task instead of scheduling `ProcessInbox` and
`FindNewestActivity` itself. The handler, on the UI thread, works out a
`GarminDownloadRequest { days, quiet }` from the settings' `garmin.days`
(30 if it isn't there) and the arguments, with
`make_download_request()` in `Garmin.h`, and gives it to the job, so the
job thread doesn't read the settings. The job reports progress with a new
`Downloading { index, count }` notification, shown in the status line as
the inbox's is, and errors as a `Message`. On Ctrl+G, but not at startup,
it also says how many activities it downloaded, or that there were none.
An activity whose `garmin-<id>.fit` is still in the inbox isn't
downloaded again.

**Rungap** goes: `storage/Rungap.h` and `Rungap.cpp`, the commented-out
`rungap` in `Storage.h`, and the webhook in the comments in
`Storage.cpp`, which now say the Garmin download writes the sidecars.

## Test plan

- `test/JSONTest.cpp`: negative integers and decimals, exponents, numbers
  too big for an integer, the range of a signed conversion, and things
  that aren't numbers.
- `test/ZipTest.cpp`: a ZIP built in the test (`test/TestZip.h`, shared
  with `GarminTest.cpp`), with a stored and a
  deflated entry; finding an entry by name or extension; a missing entry;
  a truncated or corrupt ZIP is an error, not a crash.
- `test/GarminTest.cpp`, with a fake `HttpFetch`:
  - `read_api_key()`: the environment wins over the file; whitespace and
    newlines are trimmed; no key; an empty file.
  - `parse_activity_list()`: on a trimmed copy of a real list entry,
    including `beginTimestamp` and a manual activity.
  - `obtain_token()`: the `ApiKey` header; 401 is "key rejected".
  - `list_activities()`: paging; the start date.
  - `download_new_activities()`, into a temporary store: an activity the
    store has is skipped; a new one lands in the inbox as a FIT file and
    its sidecar, with no `.part` left; a manual activity is skipped; a
    failed download leaves the earlier ones; progress counts.
- `test/ActivityLoadTest.cpp`: the sidecar's `activityName` becomes the
  title; a sidecar without one, or not JSON, leaves the FIT title.
- `Storage::has_activity()`, in a temporary store.

Checked once against Garmin, with Jan's key, into a temporary store: the
last 7 days, 5 activities, downloaded, organized by the inbox under the
start times the list gave, with their names in the sidecars; a second
run downloaded nothing.

By hand: a download at startup and on Ctrl+G with Jan's key, the status
line while it runs, the messages for a missing and a rejected key, and the
titles in the activity list and the header.

## Review notes

*Filled in by the Reviewer.*
