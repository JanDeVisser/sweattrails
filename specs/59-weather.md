# Weather

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

An activity shows the weather it was done in: the temperature, how it
felt, the wind and where it came from, and the conditions ("Clear",
"Light rain"). A slow run on a hot, humid day, or a ride into a 30 km/h
headwind, makes sense once the weather is next to it. For every outdoor
activity in the store, back to 2009, not only the recent ones.

## Feature

### What there is now

- **Strava's export** (`activities.csv`, `45-strava-export-import.md`)
  has weather columns: Weather Condition, Weather Temperature, Apparent
  Temperature, Dewpoint, Humidity, Weather Pressure, Wind Speed, Wind
  Gust, Wind Bearing, Precipitation Intensity and Probability, Cloud
  Cover, Visibility, UV Index, Sunrise and Sunset Time. In Jan's export
  (6,281 activities) they're filled for **1,749**, all from **2020
  on**, and about half of each year's activities then (the outdoor
  ones, presumably). Nothing before 2020.
- **Klimat.app** wrote the weather into Strava's descriptions ("Clear,
  15°C, Feels like 15°C, Humidity 61%, Wind 7km/h from ENE - by
  Klimat.app") for 305 activities in 2019 and 97 in 2020. That's text in
  the description, which SweatTrails shows as it is.
- SweatTrails has no weather of its own.

### Where the weather comes from

**Open-Meteo's historical weather API**
(`archive-api.open-meteo.com/v1/archive`):

- Free for non-commercial use, **no account or API key**.
- Hourly, anywhere, from 1940 up to about five days ago (ERA5
  reanalysis, a weather model fitted to observations, on a grid of
  about 25 km at worst; not a station's readings).
- Per hour: temperature, apparent temperature, humidity, precipitation,
  a WMO weather code (clear, cloudy, fog, drizzle, rain, snow,
  thunderstorm, ...), cloud cover, wind speed, direction and gusts at
  10 m.
- Limits: 10,000 requests a day, 5,000 an hour, 600 a minute.
- Checked against Klimat.app's description of Muskoka 70.3 2019's T1,
  at 12:23 UTC, near Huntsville: Klimat "Clear, 15°C, Humidity 61%,
  Wind 7km/h from ENE"; Open-Meteo at 12:00 "clear (code 0), 15.3°C,
  64%, wind 11 km/h from 49° (NE), gusts 26 km/h". Close enough to
  tell a hot day from a cold one and a headwind from a tailwind.

All the weather comes from Open-Meteo, and none from Strava's export:
one source, with one meaning, back to 2009, where Strava's covers only
2020 on. Open-Meteo's terms (CC BY 4.0) ask for attribution: the box
says "Weather data by Open-Meteo.com".

### At the start and at the end

The weather at the **start**, where and when the activity started, and
at the **end**, where and when it ended: a loop's start and end are the
same place at different times, a point-to-point ride's are two places,
and the end's weather is the end's place's. "Where" is the first and the
last record with a position, as the GPS can start late or drop out
before the end; "when" is that record's time.
A long ride, or a race, shows how it changed (Muskoka 70.3 2019's bike:
15°C at the start, 21°C at the end).

Open-Meteo's data is hourly: each is the **hour nearest** the time.
Between the two hours around it would be smoother for the temperature,
but the conditions ("Clear", "Rain") can't be in between.

On a **multisport** activity (`56-multisport.md`), it's the start and
end of the tab on display: a leg's on its tab, the whole race's on
Whole.

### The weather box

A popup like the activity info box (Cmd+I, `48-device-info.md`), for the
activity on display: **Cmd+Shift+I** (Ctrl+Shift+I off macOS, as Cmd+I
is Ctrl+I there), or "Weather" in the command palette, on the activity
screen. Esc closes it. Cmd+W would be the platform's "close window",
and W alone is the summaries' "by week". Two headings, **Start** and
**End**, each with its time, and under it:

- the conditions, from the WMO weather code: "Clear", "Partly cloudy",
  "Fog", "Light rain", "Heavy snow", "Thunderstorm", ...
- the temperature, and what it felt like: "15°C, feels like 13°C"
- the wind, its direction and the gusts: "11 km/h from the NE, gusts
  26 km/h"
- humidity, precipitation and cloud cover, in the smaller font: "64%
  humidity · no rain · 0% cloud"

°C and km/h, as the rest of the app, and the wind's direction as one of
16 compass points (N, NNE, NE, ENE, ...): where it comes **from**. Weather
from the forecast (below) says so after its time: "Start, 8:02 · from
the forecast".

and at the bottom "Weather data by Open-Meteo.com", and the hint (Esc).

Like the info box's rows, this is text that tests can check without
fonts. The two boxes probably share a widget for "a popup of headings,
entries and details" that belongs in `src/Lib/Widget`: for the
Developer to see.

### Getting it, and keeping it

- **When the box opens** on an activity without weather, it asks
  Open-Meteo, with "Getting the weather from Open-Meteo..." in the box
  until it comes. One request does both the start and the end: the API
  takes several places at once. It doesn't hold up the app: it runs as
  a job, as the downloads do.
- It's **kept in the activity's sidecar**, under `"weather"`: for the
  start and the end, the time and place it's for, and Open-Meteo's
  numbers (°C, km/h, degrees, %, mm, the WMO code), not the box's text,
  and whether it's the archive's or the forecast's. The archive's is
  kept for good: a reanalysis of the past doesn't change. Opening the
  box again shows it without asking.
- **The last few days**: the archive is about five days behind, and
  gives empty values for the hours it doesn't have yet. For those, the
  forecast API, which has the past days too, is asked, and its weather
  is kept marked as the forecast's. When the box opens on an activity
  whose weather is the forecast's, it asks the archive again, and
  replaces it once the archive has it: a ride looked at the same
  evening shows the forecast's weather, and the archive's from the
  first time the box opens after about five days. No date is
  hard-coded: the archive's empty values say when it doesn't have it.
- **Errors**: no network, Open-Meteo failing or over its limit (10,000
  requests a day): the box says why ("Couldn't get the weather:
  Open-Meteo's limit for today is reached"), nothing is kept, and it
  asks again the next time the box opens.

### No position, no weather

An activity without a position, such as one on the trainer or the
treadmill, or with no GPS, has no weather: the box says "No weather:
this activity has no position." A **Zwift** activity has positions,
but in its worlds mapped onto real places (Watopia onto the Solomon
Islands): it has no weather either, and the box says "No weather: this
is an indoor activity." So does any other activity the FIT file says is
indoors or virtual.

### Klimat.app

Klimat.app's lines in the descriptions of 2019 and 2020 are left as
they are: they're part of the description.

### Version 1 scope

Everything above. Deferred:

- *(deferred)* Getting the weather for the whole store in the
  background: 10,000 requests a day would do it in one, but it's only
  worth it once something else uses the weather, such as the summaries
  ("ran slower in the heat") or a search.
- *(deferred)* The weather along the way, for long rides and races: a
  point per hour, or per lap, where the activity was then, with a
  request for all of them at once, as the start and end are. It could
  show in the box as more rows between Start and End, or on the map
  and the graph, and is where the wind relative to the route (below)
  would come from. Jan likes it: a spec of its own after this one.
- *(deferred)* The weather as overlay fields (`13-overlay-fields.md`).
- *(deferred)* The wind relative to the route: headwind, tailwind.
- *(deferred)* Strava's export's weather columns: not used.

## Open questions

None.

## Architecture

Four parts, each testable without a window or the network.

### The weather (`storage/Weather.h`, `.cpp`, new)

- `struct WeatherPoint { i64 time; Coordinates position; }`: where and
  when the weather is for. `weather_points(Activity const &)` gives the
  start's and the end's, from the first and last record with a position
  (`Coordinates`' own check for "no fix"), or why there's no weather:
  `NoWeather::Indoor` for a Zwift file (`manufacturer`), a `sub_sport`
  of `virtual_activity`, `treadmill` or any `indoor_*`, checked first;
  else `NoWeather::NoPosition`. A leg's tab's activity (`leg_activity()`)
  has only its own records, so a leg's points come out of the same
  function.
- `struct Weather`: the point, the hour it's for (Unix time), the source
  (`WeatherSource`, an X-macro: `archive`, `forecast`), and Open-Meteo's
  numbers: temperature, apparent temperature, WMO code, and, optional as
  Open-Meteo can leave them out, humidity, precipitation, cloud cover,
  wind speed, direction and gusts. `encode()`/`decode()` as
  `StravaTitle`'s. `struct ActivityWeather { Weather start; Weather end; }`.
- `open_meteo_request(WeatherSource, std::span<WeatherPoint const>)` →
  `HttpRequest`: both points in one request (`latitude=a,b&longitude=c,d`),
  the UTC dates from the start's to the end's, the hourly variables,
  `timeformat=unixtime`. Pure, tested on its URL.
- `parse_open_meteo(body, points, source)` → per point the nearest hour's
  `Weather`, or nothing where that hour's temperature or code is empty (the
  archive not having it yet). A JSON error, or `"error": true` with its
  `"reason"`, is an error.
- `fetch_weather(HttpFetch const &, points)` → `ActivityWeather` or a
  `LibCError` with the box's message: asks the archive, and the forecast
  for the points the archive has empty. HTTP 429 says "Open-Meteo's limit
  for today is reached"; another status says it with Open-Meteo's reason.
  Takes an `HttpFetch`, so tests give it canned responses.
- `sidecar_weather(sidecar, leg)` and `save_sidecar_weather(sidecar,
  weather, leg)`, with `update_sidecar()`: under `"weather"`, or under
  `"legs"[leg]."weather"` for a leg, next to its titles. It goes in the
  shown recording's own `sidecar`, not `activity_sidecar`: it's that
  recording's start and end. Saved weather whose points aren't the
  activity's points now (its gaps filled, another recording made main) is
  asked for again.

### The text (`widget/Weather.h`, `.cpp`, new)

- `weather_condition(u8 code)`: "Clear", "Mainly clear", "Partly cloudy",
  "Overcast", "Fog", "Light drizzle", ..., "Thunderstorm with hail";
  "Unknown weather" for a code that isn't WMO's.
- `compass_point(float degrees)`: one of 16, N for 349°–11°.
- `weather_rows(WeatherState const &)` → `std::vector<InfoRow>`, where
  `WeatherState` is what the box knows: no weather and why, getting it,
  couldn't get it and why, or an `ActivityWeather` (with "getting it" for
  the forecast's while the archive is asked again). The rows are the
  spec's: heading "Start, 8:02" (local time, from `utc_offset`) with
  " · from the forecast" as needed; the conditions; "15°C, feels like
  13°C"; "11 km/h from the NE, gusts 26 km/h"; the small "64% humidity ·
  no rain · 0% cloud" ("1.2 mm rain"). Missing values are left out.

### The popups (`src/Lib/Widget/InfoBox.h`, `.cpp`, new)

The two boxes share a widget, as the spec guessed:

- `InfoLine` and `InfoRow` move from the app into `InfoBox.h`, as they
  are, with `InfoRow::file` renamed `link`: a row a click does something
  with.
- `struct InfoBox : Modal` has the measuring and drawing that
  `ActivityInfoBox` has now, of `title()`, `rows()` and `hint(now)`,
  which a subclass gives, and Esc. A click calls `click_row(index)` or
  `click_hint()`, which do nothing unless overridden.
- `ActivityInfoBox` becomes an `InfoBox`: its F, C and Tab keys, its
  copying and revealing stay as they are, and its tests keep passing.
- `WeatherBox : InfoBox` (in `widget/Weather.h`) takes callbacks, as the
  info box does: `Describe` (the activity on display's points, or why
  none, and its sidecar and leg), `Fetch` (starts getting the weather for
  points). `weather_arrived(points, result)` is how the answer comes back:
  for other points than the box's (the box closed and opened on another
  activity), it's ignored. Tests drive it without a network.
- `InfoBox.h` goes into `.clang-format`'s Widget group.

### The app

- `st-weather`, "Weather", Cmd+Shift+I (Ctrl+Shift+I off macOS), on the
  activity screen.
- **Not a job.** Jobs run one at a time, and a download holds the job
  thread for its whole run, including waiting out Strava's or Wahoo's
  limit for up to an hour: the box would say "Getting the weather..."
  all that time. The request runs on a thread of its own instead, as the
  login's wait does: a `std::jthread` member, started per request, that
  calls `fetch_weather(http_fetch, ...)` and sends a `WeatherArrived`
  notification. One request at a time: a new one while the last runs is
  started when it's done.
- The notification handler saves the weather in the sidecar, on the UI
  thread as the other sidecar writes are, then hands it to the box if
  it's open. Nothing is saved for an error.
- The box shows the tab on display when it opens. Tab doesn't switch legs
  or recordings while it's open: close and reopen it.

## Test plan

- `weather_test` (new): `weather_points()` on activities made by hand:
  first and last positioned record, records without a position at either
  end, no position at all, Zwift, `virtual_activity`, `indoor_cycling`,
  `treadmill`, a leg's activity. `open_meteo_request()`'s URLs for the
  archive and forecast, one day and over midnight. `parse_open_meteo()`
  on a captured two-place response: nearest hour (before and after the
  half hour), empty hours, a missing gust, `"error": true`, bad JSON.
  `fetch_weather()` with a canned `HttpFetch`: archive only; archive
  empty for the end, so the forecast is asked for and marked; 429; no
  response. The sidecar: save and read back, for the activity and a leg,
  keeping the titles next to it; points that don't match.
- `weather_box_test` (new, `ScopedTestApp`): `weather_condition()` for
  each WMO group and an unknown code; `compass_point()` around the
  circle, at 0°, 11.24°, 11.25°, 359°; `weather_rows()` for each state;
  the box: no position, indoor, saved archive weather shown without
  asking, none saved asks, the forecast's shown and asked again, an
  answer for other points ignored, an error shown, Esc closes.
- `activity_info_box_test` keeps passing on `InfoBox`.
- By hand: the box on Muskoka 70.3 2019's legs and Whole, a recent ride
  (the forecast), a treadmill run, a Zwift ride, no network; the sidecar
  afterwards.

## Review notes

1. **Suggestion**, `src/App/SweatTrails.cpp` `get_weather()`: a request
   made while another runs is queued without checking whether the one
   running is for the same points. Open the box, Esc, reopen it on the same
   activity before the answer comes: the second request is started when the
   first is done, though its answer is already saved, and costs an API
   call. `weather_next` could be dropped when it equals the request that
   just arrived (same sidecar, leg and points).
2. **Suggestion**, `src/App/SweatTrails.cpp` `get_weather()`: the thread's
   `stop_requested()` check and `notify()` aren't atomic, so a quit
   between them can send a notification while `SweatTrails` is being
   destroyed. The window is tiny and `on_terminate()` runs first, so I
   don't see it hit in practice. Threading otherwise checks out: the
   jthread move-assignment only joins a thread that has already sent its
   notification, and `getting_weather` and `weather_next` are only used on
   the UI thread.
3. **Suggestion**, `src/Lib/Widget/InfoBox.cpp` `handle_input()`: `on_link`
   isn't gated by anything, where `ActivityInfoBox` used to return early for
   an empty `info.path`. The box's `link_area` is zero-sized until drawn
   and `CheckCollisionPointRec` is inclusive, so a mouse at exactly (0, 0)
   would show the hand cursor. Harmless; `hint_area` always had it.
4. **Missing test, added**: `fetch_weather()` with the archive empty for
   the start only (forecast asked for one place, end stays the
   archive's), and `WeatherBox` answering a failed request and then a
   good one (error cleared, forecast replaced). Both pass.

No bugs found. ActivityInfoBox behaves as before (keys F, C, Tab, Esc, clicks,
cursor), and its tests pass. `same_as()` for the saved points, the
nearest-hour arithmetic and the error paths (`TRY_EVAL`) look right. The
forecast fallback is only covered by canned tests, as the Developer said.

### Resolution

1. Fixed (c909b8a): `next_weather_request()` drops the waiting request
   when it's the one just answered; `weather_test` checks it.
2. Left as it is: the login's thread has the same gap, and the window is
   a quit between two statements, after `on_terminate()` asked the thread
   to stop. Worth fixing for both threads together if it ever shows.
3. Left as it is: harmless, and the hint always had it.
4. The tests added stay. The leg test the test plan asked for was
   missing too, and is added (719c614).
