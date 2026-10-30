# Volume summaries

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

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
