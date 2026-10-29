# Titles from Strava

Status: In progress, on branch feature/strava-titles

## Goal

An activity shows the title it has on Strava, where it's named, rather
than the one Garmin Connect or Wahoo gave it, such as "Toronto Cycling".

## Feature

### What there is now

- An activity's title comes from, lowest to highest priority
  (`specs/edit-activity-name.md`): the start time; the name in the
  download's sidecar, `<name>.json` next to the FIT file, which is
  Garmin Connect's `activityName` or Wahoo's `name`
  (`sidecar_titles()` in `storage/Activity.cpp`); and the name the user
  gave it in SweatTrails, the sidecar's `"title"`.
- Activities come in from Garmin Connect and Wahoo (`st-download`,
  Ctrl+G, and at startup), and by hand through the inbox. Strava isn't a
  source: it doesn't hand out the original FIT files.
- `storage/Strava.*` and `storage/strava/*` are translations of the old
  Zig app's Strava client, with its OAuth login and the API's types, but
  aren't built. The Wahoo download built a generic OAuth login
  (`src/App/OAuth.h`): PKCE, opening the browser, and an HTTPS server on
  `https://localhost:8090/callback`.
- The old app wrote a `.strava` file next to some activities, Strava's
  activity as JSON, with its `name` and `description`.
  `Activity::load_strava_file()`, which read it, is commented out, and
  the store's listing counts the files but nothing reads them.

### Strava's API, as of October 2026

- Each user registers their own API app at
  [strava.com/settings/api](https://www.strava.com/settings/api), with an
  "authorization callback domain", such as `localhost`. An app starts
  with a capacity of one athlete, its owner, which is all this needs.
- OAuth 2: the user logs in at `https://www.strava.com/oauth/authorize`,
  and the token request at `https://www.strava.com/oauth/token` needs
  the app's **client secret**: Strava doesn't do PKCE without one. Scope
  `activity:read_all` reads private activities too. An access token lasts
  6 hours; the refresh token gets a new one.
- `GET /api/v3/athlete/activities?after=<epoch>&before=<epoch>&per_page=200`
  lists the athlete's activities, newest first, each with its `id`,
  `name`, `start_date` (UTC), `elapsed_time` and `sport_type`. One
  request covers 200 activities.
- Rate limits: 100 read requests per 15 minutes and 1,000 a day, per app.

### Setting up

As for Wahoo, each user registers their own Strava app: SweatTrails
doesn't ship one. The README says how:

1. Create an app at
   [strava.com/settings/api](https://www.strava.com/settings/api), with
   `localhost` as its authorization callback domain.
2. Put its client ID and client secret in the settings:
   `"strava": { "client_id": "...", "client_secret": "..." }`. Strava
   can't log in without the secret, as Wahoo's PKCE login does. It's the
   secret of the user's own app, which only they can use, in their own
   settings file.
3. The mkcert certificate that Wahoo's login uses
   (`specs/wahoo-download.md`); a user without Wahoo makes it the same
   way.
4. Log in from SweatTrails (below).

A user without a `strava.client_id` doesn't use Strava: SweatTrails
never mentions it to them.

### Logging in

- As Wahoo's: the `st-strava-login` task opens the browser at Strava's
  login, with the scope `activity:read_all`, so that private activities'
  titles come too, and Strava sends the browser back to
  `https://localhost:8090/callback`, where the same server as Wahoo's
  catches it. The status line says it's waiting; after 5 minutes, it
  gives up, with a message.
- Ctrl+G asks "Log in to Strava now?" when there's a client ID but no
  login, or a login Strava no longer accepts. Startup never opens a
  browser.
- The tokens go in `strava-tokens.json` in SweatTrails' config folder,
  readable only by the user, and are refreshed when they've expired
  (after 6 hours).

### Fetching the titles

- **With every download**: `st-download` (Ctrl+G, and at startup, quiet)
  runs the Garmin and Wahoo downloads, then lists the Strava activities
  of the last N days (`strava.days`, 30 by default), and gives the
  activities in the store that match them their Strava titles. Its
  message says what Strava did, as for the other sources, such as
  "Strava: 3 new titles."
- **The history**: a new task, `st-strava-titles`, does the same from the
  store's oldest activity, or for the last `days` if it's given. Listing
  is cheap, 200 activities a request, so all titles come in one run.
- **Matching**: a Strava activity matches an activity in the store whose
  start time is within 60 seconds of its `start_date`; of several, each
  store activity takes the nearest. The same ride recorded on two
  devices gets the title on both. Strava activities that aren't in the
  store are passed over: Strava doesn't hand out the original FIT file
  *(deferred: importing them)*.
- **Where it's kept**: in the activity's sidecar, under a key of
  SweatTrails' own, `"strava"`: `{ "id": 123, "name": "...",
  "description": "..." }`. The rest of the sidecar is kept, as a rename
  keeps it (`specs/edit-activity-name.md`); an activity without a
  sidecar, such as one copied in by hand, gets one. A sidecar that isn't
  JSON isn't overwritten, and that activity is passed over.
- **The title**, from lowest to highest priority: the start time; the
  service's name (Garmin's `activityName`, Wahoo's `name`); the old
  app's `.strava` file's `name`; the sidecar's `"strava"` name; the name
  the user gave it in SweatTrails, `"title"`. A rename in SweatTrails
  still wins, and removing it brings back Strava's.
- The activity on display shows its new title right away if it changed.

### Descriptions

- Strava's list doesn't include an activity's description: it takes a
  request per activity (`GET /api/v3/activities/{id}`). So an activity's
  description is fetched once, when it's first matched, and again when
  its name on Strava has changed since. An edit to only the description
  on Strava isn't seen *(deferred)*.
- With Strava's limits, 100 requests per 15 minutes and 1,000 a day, the
  history's descriptions may take more than one run: at the limit, the
  task stops fetching descriptions, keeps the titles it has, says when
  the limit resets, and carries on next time with the ones it hasn't
  fetched. A description fetched empty is kept as `""`, so it isn't
  fetched again.
- The description is the activity's `notes`. Where an activity has one,
  the data panel shows it under the title, in the small font, wrapped,
  three lines at most, cut short with "…" beyond that.
- The old app's `.strava` file's `description` is taken as well, until a
  fetch brings one into the sidecar.

### The old app's `.strava` files

The old app's `.strava` files are read again, for their `name` and
`description`, as above: old activities get their Strava titles without
spending requests. They're never written, nor deleted.

### Errors

As for Wahoo:

- Strava can't be reached, or a request fails: a message with the error.
  Titles fetched before the failure are kept.
- The rate limit (HTTP 429): a message that says so, and when it resets.
  Titles from the list are kept, as above.
- A login that times out, is cancelled, or finds port 8090 taken, or no
  certificate: as for Wahoo.

### Checked first by the Developer

The API facts above are from what's known of Strava's API, not checked
against its documentation for this spec. Before building on them, with
Jan's app:

- That Strava accepts `https://localhost:8090/callback` with `localhost`
  as the callback domain, and that the token request needs the client
  secret.
- The rate limits, and the headers that say how much is left and when it
  resets.
- That a list entry's `start_date` is the FIT file's start time, for
  activities synced from Garmin Connect and Wahoo, so that 60 seconds is
  enough.
- That Strava's API terms allow an app showing a user their own
  activities' titles and descriptions, as this does.

### Version 1 scope

In: setting up and logging in to Strava as to Wahoo; Strava titles for
the last N days with every download, and for the whole history with
`st-strava-titles`; matching by start time within a minute; the
`"strava"` key in the sidecar; the title's priority; descriptions,
fetched once, within the rate limit, shown under the title; the old
app's `.strava` files; the README.

*(Deferred)*:

- Importing Strava activities that aren't in the store.
- Seeing an edit to only the description on Strava.
- Sending a rename in SweatTrails to Strava.

## Open questions

None.

## Architecture

### Checked against Strava's documentation

From [developers.strava.com](https://developers.strava.com/docs/) and
the [API agreement](https://www.strava.com/legal/api), October 2026:

- `localhost` and `127.0.0.1` are allowed as redirect hosts. The token
  request needs `client_secret`; there's no PKCE.
- An access token lasts 6 hours. A refresh returns a **new refresh token**
  each time, and the old one stops working at once: it's saved before
  anything else.
- `activity:read_all` reads "Only You" activities too.
- Rate limits: 200 requests per 15 minutes and 2,000 a day overall, 100
  and 1,000 for reads. The headers `X-ReadRateLimit-Limit` and
  `X-ReadRateLimit-Usage` say `<15-minute>,<daily>`. There's no reset
  header: the 15-minute window resets at :00, :15, :30 and :45, the
  daily one at midnight UTC. A request over it gets 429.
- The agreement allows showing a user's data to that user, which is all
  this does, and asks for it to be kept securely; it sets no time limit
  on keeping it.

Still for Jan to check with his app, by hand: that the browser comes
back to `https://localhost:8090/callback`, and that `start_date` is the
FIT file's start time.

### The Strava client (`storage/Strava.h`, `Strava.cpp`)

Replaces the old Zig translation in `storage/Strava.*`, which isn't
built, with a client in the shape of `storage/Wahoo.*`. The old
translation stays in git's history. `storage/strava/*`, the API's types,
isn't built either, and isn't used: this only reads a few fields.

```cpp
struct StravaError {
    enum class Kind { NotLoggedIn, RateLimited, Failed };
    Kind        kind;
    std::string message;
    long        status;     // The HTTP status, for an HTTP error
    i64         resets_at;  // Unix time, for RateLimited
};

struct StravaActivity {     // An entry in the athlete's list
    u64         id;
    i64         start_time; // Unix time, from start_date
    std::string name;
};

struct Strava {
    HttpFetch                  fetch;
    std::string                client_id, client_secret;
    fs::path                   tokens_file;
    std::optional<OAuthTokens> tokens;
    std::function<i64()>       now;

    std::string authorize_url(std::string_view state) const;
    StravaResult<void> exchange_code(std::string_view code);
    // Between `after` and `before`, Unix times, 200 to a page.
    StravaResult<std::vector<StravaActivity>> list_activities(i64 after, i64 before);
    StravaResult<std::string> description(u64 id);
};

// When the read limit that a 429 ran into resets: the next quarter hour,
// or the next midnight UTC if the day's limit is used up.
i64 rate_limit_reset(HttpResponse const &response, i64 now);
```

**Tokens**: `WahooTokens`, `read_tokens()` and `write_tokens()` move to
`OAuth.h` as `OAuthTokens`, with the PKCE verifier optional, so that
both clients keep their tokens the same way: `wahoo-tokens.json` and
`strava-tokens.json`, readable only by the user.

### Matching (`storage/StravaTitles.h`, `.cpp`)

- **`Storage::activity_starts(i64 from, i64 to)`**: the start times of
  the store's activities between two times, from the month directories'
  `<start time>.fit` names: no FIT file is read. And
  `Storage::oldest_start()`, for the history.
- **`match_strava(strava, starts)`**, pure: for each start time in the
  store, the Strava activity nearest it within 60 seconds, if any.
- **`StravaTitle { id, name, description }`**, the sidecar's `"strava"`
  object, with `description` optional: not there until it's fetched,
  `""` if it was fetched empty.
- **`update_strava_titles(strava, storage, after, before, progress)`**,
  on the job thread:
  1. Lists the Strava activities, and matches them.
  2. For each match whose sidecar doesn't have this `id` and `name`,
     writes `"strava"` with them, keeping the description only if the
     name is the same. That's all the titles, with no more requests.
  3. Then fetches the description of each match without one, until
     they're done or Strava says 429.

  It returns `StravaUpdate { titles, descriptions, waiting,
  resets_at }`: how many titles changed, how many descriptions came, how
  many are still to fetch, and when the limit resets if it was hit.
  Each activity whose title or notes changed is in `changed`.

### The sidecar (`storage/Activity.*`)

- `save_sidecar_title()`'s read-change-write of the sidecar becomes
  `update_sidecar(sidecar, change)`, which it and
  **`save_sidecar_strava(sidecar, StravaTitle)`** use: the rest of the
  sidecar is kept, a missing one is made, and one that isn't a JSON
  object is an error and isn't touched.
- `SidecarTitles` gains `strava` (the `"strava"` object's name and
  description), read in the same read of the sidecar.
- `load_from_dir()` reads the `.strava` file, if there is one, for its
  `name` and `description` (only those two fields, without
  `storage/strava/`), and takes the title from lowest to highest: start
  time, service, `.strava`, the sidecar's `"strava"`, `"title"`. `notes`
  is the sidecar's description, else the `.strava` file's.
  `original_title` stays what a rename that's cleared goes back to:
  now Strava's, if there is one.

### The downloads (`storage/Download.*`, `SweatTrails.cpp`)

- `DownloadRequest` gains `strava`, `strava_days` (`strava.days`, 30 by
  default), and `strava_client` (`strava.client_id` and
  `client_secret`; without the ID, Strava isn't set up).
  `st-download` asks for all three sources; Strava runs last, after the
  inbox has been processed, so that what Garmin and Wahoo just brought
  in gets its title too.
- `SourceOutcome` gains `Kind::Titled`, with a sentence of its own:
  "Strava: 3 new titles." / "Strava: nothing new." and, when it stopped
  at the limit, "12 descriptions are left for after 14:45."
- `DownloadReport::ask_login` becomes the name of the source to log in
  to, so that the question says Strava or Wahoo.
- **Tasks**: `st-strava-titles` (`days`, else from the store's oldest
  activity) and `st-strava-login`. The login is Wahoo's, in a shared
  `start_login()` with what differs between them (the authorize URL, the
  job that trades the code), on the same port and certificate: two
  logins can't wait at once, which the second's "port in use" message
  covers.
- New jobs `StravaTitles` and `StravaLogin`; the existing `Download`
  runs Strava too. A notification `TitlesChanged { starts }` says which
  activities' names changed: if the one on display is among them, it
  re-reads its names and notes from its sidecar.

### Notes under the title (`widget/Activity.cpp`, `StringUtil`)

- `clamp_lines(lines, max, measure, width)` next to `wrap_words()` in
  `src/Lib/Base/StringUtil`: at most `max` lines, the last cut short with
  "…" to fit the width. Pure, so a test can check it.
- `ActivityDataDisplay::draw()` draws the notes under the title in the
  small font, three lines at most, and lays out what's below from where
  they end.

## Test plan

- `StravaTest` (`strava_test`), with canned responses through
  `HttpFetch`, as `WahooTest` does:
  - `authorize_url()`'s parameters; `exchange_code()` sends the secret
    and saves the tokens; a refresh saves the new refresh token; no
    tokens, or a 401, is `NotLoggedIn`.
  - `list_activities()` pages until a short page, and reads
    `start_date`; entries without an ID or a start are skipped.
  - `description()`; a 429 is `RateLimited`, with `rate_limit_reset()`
    at the next quarter hour, or midnight UTC when the day is used up.
- `StravaTitlesTest`, in a temporary store with FIT files from
  `TestFit.h`:
  - `match_strava()`: exact, within 60 s, past it, the nearest of two,
    one Strava activity for two in the store.
  - `update_strava_titles()`: titles written to the sidecars, the rest of
    the sidecar kept; no sidecar makes one; one that isn't JSON is left;
    descriptions fetched once, and again after a rename on Strava;
    stopping at 429 keeps the titles, and the next run fetches the rest.
  - `Storage::activity_starts()` and `oldest_start()`.
- `ActivityLoadTest`: the title's priority, with each of the five; the
  `.strava` file's name and description; `notes`.
- `DownloadTest`: `make_download_request()` with Strava;
  `download_report()`'s Strava sentences and which login it asks for.
- `WahooTest` passes with `OAuthTokens`.
- `StringUtilTest`: `clamp_lines()`.
- By hand: logging in to Strava, Ctrl+G, `st-strava-titles`, the notes
  under the title, and a rename winning over Strava's title.

## Review notes

*Filled in by the Reviewer.*
