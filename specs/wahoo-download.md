# Download Activities from Wahoo

Status: In progress

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
   `offline_data`, and the redirect URI `https://localhost:8090/callback`.
   Wahoo only takes `https` redirect URIs.
2. Put its client ID in the settings:
   `"wahoo": { "client_id": "..." }`. With PKCE, the client ID isn't a
   secret.
3. Make a certificate for `localhost` with
   [mkcert](https://github.com/FiloSottile/mkcert), which the browser
   trusts: `mkcert -install` once, and then, in SweatTrails' config
   folder, `mkcert -cert-file localhost.pem -key-file localhost-key.pem
   localhost 127.0.0.1 ::1`. Wahoo sends the browser back to
   `https://localhost:8090`, so SweatTrails has to answer there over TLS,
   and a certificate the browser doesn't trust would get a warning page
   on every login. (Firefox needs `nss` for `mkcert -install`.)
4. Log in from SweatTrails (below).

A user without a `wahoo.client_id` doesn't use Wahoo: SweatTrails never
says anything about Wahoo to them.

### Logging in

- The `st-wahoo-login` task opens the browser at Wahoo's login page. The
  user logs in and allows SweatTrails access; Wahoo sends the browser
  back to `https://localhost:8090/callback`, where SweatTrails is
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
- Port 8090 is taken: a message that says so; nothing is listening, so
  nothing comes back.
- No certificate, or one that can't be read: `st-wahoo-login` says how
  to make one with mkcert, and doesn't open the browser.

### Checked first by the Developer

As with Garmin, before building on them, with Jan's sandbox app:

- That Wahoo accepts `https://localhost:8090/callback` as a redirect URI,
  and the browser trusts the mkcert certificate.
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

What Wahoo's documentation adds to "How it works now", read while
designing: the token requests take their parameters as a form body; PKCE
supports both `plain` and `S256`; **a refresh sends the `code_verifier`
again**, so the verifier is kept with the tokens; a refresh rotates both
tokens, and the old ones are revoked once the new access token is used;
`GET /v1/workouts` answers `{ workouts, total, page, per_page }`, sorted
by `starts` descending; `starts` is ISO 8601 UTC with milliseconds; every
response has `X-RateLimit-Limit`, `-Remaining` and `-Reset` (seconds
until the reset).

### Shared pieces

- **`Http.h`.** `HttpRequest` gets a `body` (the token requests POST a
  form), and `HttpResponse` gets its `headers`, with a `header(name)`
  lookup that ignores case (for `X-RateLimit-Reset`). `http_fetch()`
  sends the body and collects the headers. `url_encode()` and
  `form_encode()` for the query strings and form bodies.
- **OpenSSL, a new dependency** (`openssl@4` from Homebrew, `libssl-dev`
  on Debian and Ubuntu), for the TLS the callback needs. With it there
  anyway, PKCE's S256 challenge, `base64url(sha256(verifier))`, uses its
  SHA-256, and the verifier its `RAND_bytes()`. `base64url()` goes in
  `StringUtil.h`.
- **`src/App/OAuth.h/.cpp`**, generic, so Strava can use it later:
  - `Pkce`: a random verifier (43 to 128 characters from
    `std::random_device`), and its S256 challenge.
  - `LoopbackServer`: binds `127.0.0.1:<port>` and `[::1]:<port>` when
    made (a browser may take `localhost` to be either), and loads the
    certificate and key into an OpenSSL context, so a taken port or a
    missing certificate is an error right away, before the browser opens.
    `wait_for_redirect(timeout, stop_token)` waits for a connection with
    `poll()`, does the TLS handshake, reads one request, answers it with
    a short HTML page, and returns the query string's parameters. A
    connection that isn't a request for the callback (a failed handshake,
    the browser asking for `/favicon.ico`) is answered or dropped, and
    the wait goes on. POSIX sockets, as `wait_for_oauth_callback()` in
    Strava.cpp, which this replaces in time, with OpenSSL's `SSL` on top.
    `LoopbackServer` takes the TLS context as an argument, so a test can
    use a certificate it makes on the fly.
  - `open_browser(url)`: `open` on macOS, `xdg-open` on Linux, with
    `Process`.
- **`write_file_atomically(path, contents, perms)`** in `IO.h`: the
  `.part`-and-rename that's now `write_whole_file()` in Garmin.cpp, with
  the permissions set on the `.part` before anything is written to it.
  The Garmin download, the Wahoo download and the token file use it.

### `src/App/storage/Wahoo.h/.cpp`

Mirrors Garmin.h:

- `WahooError { Kind { NotLoggedIn, Failed }, message, status }`.
  NotLoggedIn covers no tokens and a refresh Wahoo rejects (401 or 400
  `invalid_grant`): both mean logging in again. A 429 is Failed, with a
  message that says when the limit resets.
- `WahooTokens { access_token, refresh_token, expires_at, code_verifier }`,
  with `read_tokens(path)` and `write_tokens(path, tokens)`, mode 0600.
  `wahoo_tokens_file(config_dir)` is `<config_dir>/wahoo-tokens.json`.
- `WahooWorkout { id, start_time, file_url, json }`; `parse_workouts()`
  skips workouts without an ID or start time, and leaves `file_url`
  empty for those without a FIT file.
- `Wahoo { fetch, client_id, tokens, tokens_file; ... }`:
  - `authorize_url(pkce, redirect_uri)`,
    `exchange_code(code, pkce, redirect_uri)`: what the login needs.
  - `list_workouts(since)`: pages of 30, newest first, until a workout
    started before `since`, an empty page, or `total`.
  - `download_fit(url)`: a plain GET, no `Authorization` header, unless
    the check below says otherwise.
  - Every API call first refreshes the tokens if they expire within a
    minute, and saves the new ones before using them.
- `download_new_workouts(wahoo, storage, since, progress)`: as
  `download_new_activities()`, with `wahoo-<id>.json` and `.fit`.

### Jobs and tasks

- **One download job for every source.** `DownloadGarmin` becomes
  `Download(DownloadRequest { garmin, wahoo, days, quiet })`. The job
  runs each source asked for in turn, and collects a `SourceOutcome` per
  source: not set up, needs a login, n new activities, or an error.
  `download_report(outcomes)` turns them into the one message ("Garmin
  Connect: 2 new activities. Wahoo: nothing new."): a function a test
  can reach. If anything came in, the job schedules ProcessInbox and
  FindNewestActivity, as now.
- `st-download` (Ctrl+G), `st-garmin-download` and `st-wahoo-download`
  schedule that job, with both sources or one. `make_download_request()`
  moves out of Garmin.h, into `src/App/storage/Download.h`, with the
  outcome and the report.
- When the job is done it sends `DownloadDone { report, ask_wahoo_login }`
  to the UI thread. The UI shows the report in a message box, or, when
  Wahoo needs a login and the download wasn't quiet, a `query_box` with
  the report and "Log in to Wahoo now?". Yes submits `st-wahoo-login`.
- The `Downloading` notification gets the source's name, for the status
  line. `garmin_downloading` becomes `downloading`, for any download.
- **The login runs on a thread of its own**, not as a job, so that the
  5 minutes it may wait don't hold up the inbox or a download.
  `st-wahoo-login`, on the UI thread: makes the PKCE pair, binds the
  `LoopbackServer` (a taken port is a message right away), opens the
  browser, and starts a `std::jthread` that waits for the redirect. The
  thread does nothing but wait: it hands the code, or the error, to a
  `WahooLogin` job, which trades it for tokens, saves them, and runs the
  Wahoo download. A `wahoo_logging_in` flag keeps a second login from
  starting, and keeps Ctrl+G from offering one. The jthread is a member
  of SweatTrails: on quit, its stop token ends the wait.
- **Titles.** `sidecar_title()` takes `activityName`, Garmin's, or else
  `name`, Wahoo's.

### Checked first, with Jan's sandbox app

Before the download, after the login: build the login, run it against
Jan's app, and check that the redirect URI is accepted, whether the FIT
file's URL needs the token, and that `starts` is the FIT session's start
time. Tokens and the client ID aren't printed.

## Test plan

- `OAuthTest`:
  - base64url without padding, and RFC 7636's example verifier and S256
    challenge; the verifier's length and alphabet.
  - `LoopbackServer` on port 0 (any free port), with a self-signed
    certificate the test makes and a TLS client in the test: a redirect
    with `code` and `state`, one with `error=access_denied`, a
    `/favicon.ico` before the callback, a client that never handshakes,
    a timeout, a stop request, a taken port, a missing certificate.
- `WahooTest`, with a fake `HttpFetch`, as `GarminTest`:
  - `authorize_url()` has the client ID, scopes, redirect URI and S256
    challenge, URL-encoded; `exchange_code()` POSTs the form body with
    the verifier, and saves the tokens.
  - Tokens: read and written, mode 0600; refreshed when about to expire,
    with the verifier, and saved before the next request; a rejected
    refresh is NotLoggedIn; no file is NotLoggedIn.
  - `parse_workouts()`: `starts` to Unix time; a workout without
    `workout_summary` or `file.url` has no file; odd entries skipped.
  - `list_workouts()` stops at a workout before `since`, at an empty
    page, and at `total`.
  - `download_new_workouts()`: skips what the store or the inbox has,
    writes the sidecar before the FIT file, keeps what came in before an
    error; a 429 says when the limit resets.
- `DownloadTest`: `make_download_request()` from the settings and
  arguments, for each source; `download_report()` for each combination
  of outcomes, including sources that aren't set up, which it leaves
  out.
- `ActivityLoadTest`: a sidecar with `name` gives the title.
- `GarminTest` keeps passing after `write_whole_file()` moves to `IO.h`.
- By hand, with Jan's app: log in, download, Ctrl+G again (nothing new),
  the login query when the token file is gone, a cancelled login, a
  taken port 8090, and no certificate.

## Review notes

*Filled in by the Reviewer.*
