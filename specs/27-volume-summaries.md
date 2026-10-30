# Volume summaries

Status: In progress

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

The user sees how much they've run and ridden: the distance and time per
week, month and year, as numbers and as a bar chart. They can follow how
their training builds up, and compare one period with another. Now the app shows one activity at a time, and a month's list
(Ctrl+M) is as close as it gets to a total.

This is the first of two specs. `28-volume-year-to-date.md` adds the
year-to-date lines, which compare a year with the years before, and
drilling down from a year to its months, weeks and activities.

## Feature

### What there is now

- The store has an activity per FIT file, filed by month
  (`activity/<year>/<month>/<start>.fit`), with a sidecar `<start>.json`
  for its title and laps. Jan's store has about 1,800 activities, from
  2009 on, most of them from 2021 on: 255 MB of FIT files.
- Listing a month (`Storage::listed_month()`) loads each of its
  activities shallowly: the FIT file is read through, all but the
  records and laps, for the sport, distance and times. Doing this for
  every activity in the store means reading all of those 255 MB.
- Each activity has a sport (`FIT::sport`: running, cycling, walking,
  swimming, …) and a sub-sport (treadmill, trail, indoor cycling,
  virtual activity, …), its distance, moving and elapsed time, and its
  ascent.
- The graph under the map (`ActivityGraph`) draws one activity's
  records against time. There is no bar chart, nor a graph of anything
  but an activity's records, in the app or in `src/Lib/Widget`.

### The summaries screen

- The summaries are a screen of their own, in the place of the main
  screen's map, data panel, graph and laps, as the overlay preview takes
  the map's place. Graphs of a few years of weeks need the room, and
  they have nothing to do with the activity on display. Going back
  shows the activity as it was.
- **Ctrl+T** (totals) opens the screen, and Ctrl+T or Esc closes it.
- It opens on the current year, by week, and on the sport of the
  activity on display if that's running or cycling, otherwise running.
- The graph is at the top, and a **table** of the periods it shows is
  under it: a row per period with its distance, moving time, count and
  ascent, and a row of their total. The table has the exact numbers the
  bars can't show. Hovering over a bar, or selecting a row, highlights
  the bar and the row both.

### Keys on the screen

Single letters, as there's no text to type on this screen:

- **W**, **M**, **Y**: a bar per week, month or year.
- **R**, **C**: running or cycling.
- **D**, **T**, **N**, **A**: distance, moving time, number of
  activities, ascent.
- **Left**, **Right**: a year back or forward. In the years view, and
  where there's no activity further on, they do nothing.
- **Up**, **Down**: through the table's rows, as in the laps table.
- **Esc**: closes the screen.

They're tasks, as every action is.

### Periods

- A **week** starts on Monday, as ISO weeks, Garmin and Strava count
  them: the weekend's long run ends the week.
- An activity counts in the week, month and year of its start, in the
  local time it was recorded in, as the store files it.
- Periods without activities are shown, as an empty bar and a row of
  zeros: a week off is part of the picture.
- The span shown is a **calendar year**: the weeks view shows the weeks
  that start in the year, 52 or 53, and the months view its 12 months.
  The years view shows every year from the store's first activity to
  this one. The current period, partly over, is shown as it stands.
- Activities without a distance, such as a treadmill run without a
  foot pod, add their time, count and ascent, and no distance.

### Sports

- Two totals: **running**, with treadmill and trail runs, and
  **cycling**, with indoor rides, spin and Zwift. The screen shows one
  of them at a time, and the user switches between them. Indoor
  activities count as the device recorded them: a treadmill's or
  Zwift's distance is the distance.
- Other sports, such as walks, hikes and swims, are left out.

### The numbers

- For each period: the **distance**, the **moving time**, the **number
  of activities** and the **ascent**. Distance and time are what the
  feature is for; the count and the ascent are in every activity's
  summary already, and cost nothing more.
- A graph shows one of these at a time, and the user switches between
  them.

### The bar chart

- A bar per period, its height the period's total of the number shown,
  with the periods along the bottom (weeks numbered, months and years
  named) and a scale up the side.

### The summary file

- Totalling means knowing every activity's sport, start, distance,
  moving time and ascent, and reading 1,800 FIT files for them takes
  long. So the app keeps them in a file of their own,
  `summaries.json` in the data directory, an entry per activity.
- The first time, the app reads all the activities in the background,
  with its progress on the status line, and the screen fills in as it
  goes. After that, only activities that are new, changed or gone since
  are read: those the inbox files, those downloaded, and FIT files
  changed or removed by hand.
- The file is the app's own: removing it only means it's built again.

### README

The README says how to open the summaries, and the keys on the screen.

### Version 1 scope

Everything above. In `28-volume-year-to-date.md`: the year-to-date
lines (G) and drilling down (Enter). Deferred:

- Other sports than running and cycling, and running and cycling
  together *(deferred)*.
- Weeks starting on another day than Monday, as a setting
  *(deferred)*.
- Rolling spans, such as the last 52 weeks up to now *(deferred)*.
- Goals, such as 3,000 km this year, drawn on the year-to-date lines
  *(deferred)*.
- Miles: the app shows kilometres everywhere *(deferred)*.

The bar chart has nothing of the app's own in them,
and may belong in `src/Lib/Widget`.

## Open questions

None.

## Architecture

### The summary file (`storage/Summaries.h`, `Summaries.cpp`)

- **`ActivitySummary`**: what the totals need of an activity, and what
  tells whether its FIT file changed: `file` (relative to the activity
  directory, `2025/03/1741000000.fit`), `size` and `modified` (the FIT
  file's), `start` (a Unix time), `sport` and `sub_sport` (optional:
  none for a file that can't be read, which is kept so that it isn't
  read again each time), `distance` (m), `moving` (s) and `ascent` (m).
  Every sport is kept, not only running and cycling, so that the
  deferred sports cost no new build.
- **`summaries.json`** in the data directory:

  ```json
  { "version": 1,
    "activities": [
      { "file": "2025/03/1741000000.fit", "size": 183402,
        "modified": 1741012345, "start": 1741000000,
        "sport": 2, "sub_sport": 58,
        "distance": 42310.5, "moving": 5530, "ascent": 412 } ] }
  ```

  The sport and sub-sport are FIT's numbers for them (cycling, virtual
  activity), as there's nothing to read FIT's names back with.
  `read_summaries(path)` and `write_summaries(path, summaries)`, written
  to a temporary file and renamed, so that a crash leaves the old file.
  A file that can't be read, or of another version, is no file: it's
  built again.
- **`SummaryWork plan_update(summaries, files)`**, pure: given the
  summaries and the FIT files there are now (`file`, `size`,
  `modified`), the summaries to keep and the files to read: new ones,
  and ones whose size or time changed. Ones whose file is gone drop out.
- **The job, `UpdateSummaries`**: lists the FIT files (from
  `activity_starts()`, by name, and their size and time), plans the
  update, and reads the files to read `SummaryChunk` (50) at a time,
  shallowly, as a month's list does. After each chunk it writes the
  file, sends `SummariesUpdated { summaries, read, to_read }` with
  `notify_latest()`, and schedules itself for the next chunk. So the
  first build, some 1,800 files, doesn't hold up a download or the
  inbox for minutes: they run between its chunks.
- It's scheduled at the start, after the newest activity is shown, and
  after the inbox and a download have filed anything. Reading a FIT file
  removed by hand is noticed at the next start.
- On the UI thread, `SweatTrails::summaries` holds the latest, and the
  status line says "Reading activities for the totals: 340 of 1,802"
  while there are files to read, and nothing when it's done.

### The totals (`storage/Volume.h`, `Volume.cpp`), pure

- **`VolumeSport { Running, Cycling }`** and
  **`std::optional<VolumeSport> volume_sport(sport, sub_sport)`**:
  running, or a treadmill or indoor run under another sport, is
  Running; cycling, or an indoor, spin or virtual ride under another
  sport (such as fitness equipment, as older Garmins record a spin
  bike), is Cycling; the rest is nothing.
- **`Period { Week, Month, Year }`** and **`Metric { Distance, Time,
  Count, Ascent }`**, X-macro enums.
- **`PeriodTotal`**: `first_day` and `last_day` (dates), `distance`,
  `moving`, `count`, `ascent`.
- **`std::vector<PeriodTotal> volume_totals(summaries, sport, period,
  year)`**: the year's weeks (those whose Monday is in it), its 12
  months, or, for `Year`, every year from the sport's first activity to
  `year`, each with its activities' totals, the empty ones too. An
  activity's day is its start in the computer's time zone,
  `DateTime::from_timestamp()`, as the store files it and the month list
  shows it (the spec's "local time it was recorded in" is that, for an
  activity recorded at home).
- **`std::pair<int, int> volume_years(summaries, sport, today)`**: the
  years Left and Right go between: the sport's first activity's year to
  this year.
- Dates use `std::chrono::year_month_day` and `weekday` for the
  Mondays, which libc++ has (it's only `zoned_time` it hasn't).

### The bar chart (`src/Lib/Widget/BarChart.h`, `BarChart.cpp`)

A widget of its own, as it knows nothing of activities:

- `bars`: a `label` (may be empty) and a `value` each;
  `format_value(double) -> string` for the scale; `highlight`, an
  optional index; `on_hover(optional<size_t>)` and `on_click(size_t)`.
- `ChartScale nice_scale(double max, int ticks)`, pure: a top and a step
  of 1, 2 or 5 times a power of ten, so that the scale reads 0, 50, 100,
  150 km and not 0, 47.3, 94.6.
- `bar_at(x)`, pure geometry, for the hover and the click. Labels that
  would overlap the one before are left out: the weeks label only the
  first week of each month, with the month's name, and the chart drops
  more if it's narrow.

### The screen (`widget/Summaries.h`, `Summaries.cpp`)

- **`SummaryView`**, pure: `sport`, `metric`, `period`, `year`, and the
  years it may go between; `set_period()`, `set_sport()`, `set_metric()`,
  `step_year(±1)`, which keep `year` in its range.
- **`SummariesScreen : Layout`**: a heading ("Cycling · distance by
  week · 2025"), a line of the keys, the `BarChart`, and a `Table`
  (Lib/Widget's, as for the laps) of the periods and a total row:
  Week ("Mon 3 Mar"), Month ("March") or Year, Distance, Time, Rides or
  Runs, Ascent. `set_summaries()` and the view's changes rebuild the
  bars and the rows from `volume_totals()`.
- The bar's value is the metric in the unit shown: km, hours, a count,
  m. The table's distance uses `format_distance()`; its time is hours
  and minutes ("312:10"), as `format_moving_time()`'s seconds mean
  little in a year's total.
- Hovering over a bar selects its row in the table, which scrolls to
  it; selecting a row (Up, Down, a click) highlights its bar.
- `ActivityDisplay` and `SummariesScreen` are both in the app's
  `WidgetStack`; Ctrl+T puts one or the other in front. Choosing an
  activity elsewhere, from the month list (Ctrl+M) or a download, shows
  the activity screen again.

### Keys (`src/Lib/Widget/Widget.h`, `SweatTrails.cpp`)

- Left, Right, Up, Down and Esc are bound now to the activity screen's
  tasks, and the first binding of a key wins. **`Task::when(predicate)`**,
  new in `AppBus`: a task whose predicate is false is passed over, and
  the next binding of the key gets it. The summaries' tasks
  (`st-summaries-*`) are `when` the summaries are showing; the activity
  screen's (the laps, the activities, Esc, the map's zoom, renaming,
  laps, the overlay keys) `when` they aren't, so that they don't act on
  an activity that isn't on the screen. Ctrl+M, Ctrl+G, Ctrl+Q and
  Ctrl+T work on both.
- `st-summaries` (Ctrl+T) opens and closes the screen; the others take
  their choice as an argument where they can (`st-summaries-period`
  with `"week"`, …), so that W, M and Y are one task.

## Test plan

- `SummariesTest`: `read_summaries()` and `write_summaries()` round
  trip, a missing, broken or other-version file is none; `plan_update()`
  keeps, rereads a changed size or time, reads a new file, and drops a
  gone one; an unreadable file's entry stays without a sport.
- `VolumeTest`: `volume_sport()` for running, treadmill, cycling,
  virtual, spin under fitness equipment, and walking (none);
  `volume_totals()` by week (2024 has 53 Mondays, 2025 52), a week
  across New Year, by month,
  by year from the first activity, empty periods, an activity without
  distance; `volume_years()`.
- `BarChartTest`: `nice_scale()` for 0, 1, 47, 100, 3,120; `bar_at()`
  at the edges and between bars; labels that overlap are dropped.
- `SummariesScreenTest` (`TestApp`, no window): `SummaryView`'s keys
  keep the year in range; the rows and the total row for a few
  activities; hovering a bar selects the row.
- `AppBus`: `when()` passes a key to the next binding, and a task
  without one works as before.
- The job is tried by hand, on a copy of a store, not Jan's: the first
  build's progress, a second start reading nothing, a new FIT file
  read. The drawing of the chart and the screen by hand.

## Review notes

*Filled in by the Reviewer.*
