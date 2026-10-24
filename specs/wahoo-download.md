# Download Activities from Wahoo

Status: Ready

## Goal

Activities recorded on a Wahoo device or app (an ELEMNT bike computer,
the Wahoo app, SYSTM) show up in SweatTrails without copying FIT files by
hand, as Garmin Connect's do since
[garmin-download.md](garmin-download.md).

## How it works now

### What the Garmin download gives this feature

- The `st-garmin-download` task, bound to Ctrl+G, runs a `DownloadGarmin`
  job. It lists the last `garmin.days` days, skips activities the store
  has (`Storage::has_activity()`, by start time), and writes each new one
  to the inbox as `garmin-<id>.json`, the activity's summary, and then
  `garmin-<id>.fit`, each through a `.part` file. If anything came in, the
  job processes the inbox and shows the newest activity. At startup the
  task runs with `quiet`, after the inbox and the newest activity.
- `HttpFetch` and `http_fetch()` (`src/App/Http.h`): HTTP requests with
  libcurl, replaceable in tests.
- The sidecar `<stem>.json` travels with its FIT file through the inbox.
  `sidecar_title()` takes the title from its `activityName`, which is
  Garmin's field name.
- A `Downloading { index, count }` notification shows progress in the
  status line, with "from Garmin Connect" in the text.
- Garmin's key is read from `~/.config/garmin/connect-personal-api-key`,
  where Garmin's own tools look. Nothing is written: the user puts the key
  there.

### Strava's OAuth, which isn't built

`storage/Strava.*`, translated from the Zig version and not in
`CMakeLists.txt`, has the OAuth 2 authorization code flow: it opens the
browser at Strava's authorize URL, and catches the redirect on a small
local server at `http://localhost:8089/callback`, with raw POSIX sockets
(`wait_for_oauth_callback()`). It keeps the client ID and secret, the
tokens and their expiry in a config file. That's the shape Wahoo needs
too.

### Wahoo's Cloud API, as of October 2026

From Wahoo's documentation at
[cloud-api.wahooligan.com](https://cloud-api.wahooligan.com/):

- **Apps.** A developer registers an app on
  [developers.wahooligan.com](https://developers.wahooligan.com), with its
  redirect URIs. A *sandbox* app works right away, with low limits: 25
  requests per 5 minutes, 100 an hour, 250 a day. A *production* app goes
  through a review by Wahoo, for 200 per 5 minutes, 1,000 an hour, 5,000 a
  day. Logging in, refreshing tokens and downloading files don't count.
- **OAuth 2.** The user logs in at
  `https://api.wahooligan.com/oauth/authorize` (`client_id`,
  `redirect_uri`, `scope`, `response_type=code`), and is sent back to
  `<redirect_uri>?code=<code>`. `https://api.wahooligan.com/oauth/token`
  trades the code for tokens. With PKCE (`code_challenge`,
  `code_verifier`), and the app marked as not confidential, no client
  secret is needed. Scopes: `workouts_read`, `user_read` (without it, the
  API answers 403), and `offline_data` for a refresh token.
- **Tokens.** An access token lasts 2 hours; the refresh token gets a new
  one. Unrevoked access tokens are deleted 60 days after they're made, and
  since January 2026 an app may have at most 10 unrevoked tokens per user.
- **Workouts.** `GET /v1/workouts`, newest first, with `page` and
  `per_page` (30 by default), and no date filter. A workout has a `name`,
  a `starts` time, and a `workout_summary` whose `file.url` is the FIT
  file, on Wahoo's CDN.
- **Only Wahoo's own workouts.** Workouts that came into Wahoo from
  third-party apps aren't shared.
- Webhooks exist, but need a public server, which a desktop app doesn't
  have.

Second-hand, not in Wahoo's documentation: the FIT file's URL needs no
`Authorization` header.

## Feature

Jan records rides on a Wahoo ELEMNT bike computer, and runs, and some
rides, on a Garmin Forerunner.

### Setting up

Each user registers their own Wahoo app, as each user creates their own
Garmin key: SweatTrails doesn't ship a client ID. A sandbox app works
right away, and its limit of 250 requests a day is plenty for one person:
a download is a request or two, and the FIT files don't count. A
production app, with a client ID SweatTrails ships, would need Wahoo's
review first *(deferred)*.

The README says how:

1. Register an app at
   [developers.wahooligan.com](https://developers.wahooligan.com): a
   sandbox app, not confidential (so it logs in with PKCE, without a
   client secret), with the scopes `workouts_read`, `user_read` and
   `offline_data`, and the redirect URI `http://localhost:8089/callback`.
2. Put its client ID in the settings:
   `"wahoo": { "client_id": "..." }`. With PKCE, the client ID isn't a
   secret.
3. Log in from SweatTrails (below).

A user without a `wahoo.client_id` doesn't use Wahoo: SweatTrails never
says anything about Wahoo to them.

### Logging in

- The `st-wahoo-login` task opens the browser at Wahoo's login page. The
  user logs in and allows SweatTrails access; Wahoo sends the browser
  back to `http://localhost:8089/callback`, where SweatTrails is
  listening, and the page says the login worked and the window can be
  closed. SweatTrails trades the code for tokens, saves them, and
  downloads.
- While it waits, the status line says "Waiting for the Wahoo login in
  your browser", and the app keeps working: the activity display, the
  map tiles, and the Garmin download don't wait for the login. After 5
  minutes without a login, it gives up, with a message.
- `st-wahoo-login` has no key: until the command palette exists, Ctrl+G
  offers it. When Ctrl+G finds a client ID but no login, or a login Wahoo
  no longer accepts, it asks "Log in to Wahoo now?", and Yes runs
  `st-wahoo-login`. Startup never opens a browser by itself, and says
  nothing.
- The tokens go in `wahoo-tokens.json` in SweatTrails' config folder
  (`~/Library/Application Support/sweattrails` on macOS,
  `~/.config/sweattrails` on Linux), readable only by the user. An access
  token lasts 2 hours; SweatTrails refreshes it, and saves the new tokens,
  when it's expired. A refresh Wahoo rejects (Wahoo deletes tokens unused
  for 60 days) means logging in again.

### Downloading

- The `st-wahoo-download` task downloads the workouts of the last N days
  (`wahoo.days` in the settings, 30 by default) that the store doesn't
  have, into the inbox, as `wahoo-<id>.json`, the workout as Wahoo sent
  it, and then `wahoo-<id>.fit`, as the Garmin download does. If any came
  in, the inbox is processed and the newest activity shown. It takes the
  same `days` and `quiet` arguments as `st-garmin-download`.
- Wahoo's list has no date filter: it's newest first, and the download
  pages until it reaches a workout that started before the N days.
- A workout without a FIT file (no `workout_summary`, or no `file.url`
  in it) is skipped.
- The status line counts, as for Garmin: "Downloading 1 of 2 from Wahoo".
- The workout's `name` becomes the activity's title, as Garmin's
  `activityName` does.

### Downloading from every source

- A new task, `st-download`, runs the Garmin download and then the Wahoo
  one. Ctrl+G is bound to it now, instead of to `st-garmin-download`,
  which stays a task, for the palette. Startup runs `st-download` with
  `quiet`. A `days` argument goes to both.
- A source that isn't set up is skipped: Garmin without a key, Wahoo
  without a client ID. On Ctrl+G, one message says what each source that
  is set up brought in, rather than a message box each, for example
  "Garmin Connect: 2 new activities. Wahoo: nothing new."

### The same ride on two devices

When Jan records a ride on the ELEMNT and on the Forerunner, both
downloads bring it in. Their start times differ by a few seconds, so the
store doesn't see them as the same activity, and the list shows both.
That's accepted for now: telling that two activities are the same ride,
and which one to keep, is a feature of its own *(deferred)*.

### Errors

- Wahoo can't be reached, or a request fails: a message with the error,
  as for Garmin. Workouts downloaded before the failure stay in the inbox
  and are processed.
- Wahoo's rate limit (HTTP 429): a message that says so, and when the
  limit resets (`X-RateLimit-Reset`).
- A login that times out, or that the user cancels in the browser: a
  message, and nothing is saved.
- Port 8089 is taken: a message that says so; nothing is listening, so
  nothing comes back.

### Checked first by the Developer

As with Garmin, before building on them, with Jan's sandbox app:

- That Wahoo accepts `http://localhost:8089/callback` as a redirect URI.
  If it doesn't, the Developer brings the alternatives to Jan.
- That a workout's `starts` is the FIT session's start time to the
  second, so `Storage::has_activity()` recognizes workouts the store has.
  If not, the sidecars' workout IDs tell instead.
- Whether the FIT file's URL needs an `Authorization` header.

### Version 1 scope

Everything above. Deferred: a production app with a shipped client ID
*(deferred)*; recognizing the same ride from two devices *(deferred)*;
webhooks, which need a public server *(deferred)*; Wahoo's routes and
plans *(deferred)*.

## Open questions

None.

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
