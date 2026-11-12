# Living with Wahoo's and Strava's rate limits

Status: Ready

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

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
