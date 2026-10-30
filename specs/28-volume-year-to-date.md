# Volume summaries: year to date, and drilling down

Status: Done

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

On the summaries screen, the user compares a year with the years before,
the total so far against the same day of earlier years: ahead of last
year or behind, and by how much. And they go from a year to its months,
from a month to its weeks, and from a week to its activities, to find
what made a big week big.

## Feature

### What there is now

`27-volume-summaries.md` built the summaries screen (Ctrl+T): a bar
chart of the running or cycling totals per week, month or year of a
calendar year, a table of the same under it, and keys to pick the
period (W, M, Y), the sport (R, C), the number (D, T, N, A) and the
year (Left, Right). Esc closes the screen. This spec adds to that
screen.

### Bars or lines

- **G** switches the graph between the bars and the year-to-date
  lines. The sport, the number and the year stay as they are.

### The year-to-date lines

- Three lines: the year shown, bold, and the two years before it,
  fainter, with a legend saying which year is which. Left and Right
  move the year. More lines than three are hard to tell apart.
- Across the graph runs the year, January to December; up it the total
  so far of the number shown (distance, time, count or ascent). The
  current year's line stops at today.
- The table under the lines has a row per month, the totals so far at
  its end of the number shown: in the year shown, in the year before,
  and the difference, so that "ahead of last year by 120 km" can be
  read off.

### Drilling down

- **Enter** on a row, or a click on a bar:
  - a year shows its months;
  - a month shows its weeks, the ones that start in it;
  - a week opens the activity list (Ctrl+M) at that week's first
    activity. Choosing one shows it on the main screen, as from the list
    anywhere; Esc in the list goes back to the summaries. A week without
    activities does nothing.
- **Esc** or **Backspace** goes back up: from a month's weeks to the
  year's months, from a year's months to the years, as the user came
  down. At the top, Esc closes the screen, as it did before. W, M and
  Y start a level afresh.
- In the year-to-date lines there are no periods to drill into: Enter
  and a click do nothing there.

### README

The README adds G, Enter and Backspace to the summaries' keys.

### Version 1 scope

Everything above. Deferred: goals, such as 3,000 km this year, drawn on
the year-to-date lines *(deferred)*.

The line graph has nothing of the app's own in it, and may belong in
`src/Lib/Widget`, next to 27's bar chart.

## Open questions

None.

## Architecture

### Where the spec leaves a gap

- **W, M and Y in the lines** switch back to the bars, by that period:
  choosing a period is choosing bars. G goes back to the lines.
- **Left and Right in a month's weeks** go a month back or forward,
  across New Year too, as the weeks of one month are what's shown, from
  the month of the sport's first activity to this one
  (`volume_months()`). Elsewhere they go a year, as now.
- **A click on a table row** selects it, as now; Enter drills. Only a
  click on a bar drills, as the spec says: a click in the table is how
  the user selects a row to read.
- **The year-to-date table** leaves the months after this one empty in
  the current year: there's no total so far yet. This month has the
  total up to today, against the year before's at the same date (the
  28th of February for the 29th), and says so: "June to the 15th".
- **Years before the sport's first activity** get no line, and aren't
  in the legend: a flat line at zero would read as a year off. In the
  table, the year before is empty for the sport's first year.
- **Going back up** keeps the year on display, which Left, Right, R and
  C may have changed since drilling in, and selects its month.
- **A line of one value**, this year's on New Year's Day, is a dot.

### The totals (`storage/Volume.*`), pure

- `volume_totals(summaries, sport, period, year, month)`: an optional
  `month`, for a month's weeks: the weeks whose Monday is in it.
- **`std::vector<double> year_to_date(summaries, sport, metric, year,
  until)`**: the total so far of the metric, in its own units, at the
  end of each day of the year, January 1st first; up to and including
  `until` (today) if it's in the year, else the whole year. Days are
  the computer's, as for the weeks.
- **`YearToDateRow`**: a month, the total so far at its end in the year
  and in the year before (optional: none after today), and `until`,
  today's day for this month, whose totals go to today; and
  **`std::vector<YearToDateRow> year_to_date_months(…)`** from the
  daily totals.

### The line chart (`src/Lib/Widget/LineChart.*`)

- `nice_scale()` and `visible_labels()` move from `BarChart.h` to a
  `Chart.h` that both charts include.
- **`LineChart`**: `series`, each a `label`, a `color`, `bold`, and
  `values` at x = 0, 1, 2, …; `x_count`, the width of the x axis in
  values (365 or 366 days), so that a line that stops at today stops
  partway; `ticks`, labels at x positions (the months' first days);
  `format_value` and `min_step` as the bar chart's; a legend of the
  series' labels, top left. Pure: `x_at()` and `y_at()` geometry,
  tested as `bar_at()` is.

### The screen (`widget/Summaries.*`)

- `SummaryView` gains `lines` (bars or lines) and `month` (optional,
  for a month's weeks). `step_year()` steps a month instead when there
  is one.
- **Drilling**: `SummariesScreen::trail`, the views the user came down
  from. `drill()` pushes the view and goes down: a year to its months,
  a month to its weeks; a week calls `on_week(first activity)`, which
  the app sets to open the month list there. `up()` pops the trail,
  and says whether there was anything to pop, for Esc to close the
  screen when there wasn't. W, M, Y and G clear the trail.
- The selection when going up is the period the user drilled into, so
  that Backspace leaves them where they were.
- The chart and the lines are both in the screen; the one not shown is
  `Hide`, as the overlay preview and the map are.
- The year-to-date rows: Month, the year, the year before, and the
  difference, signed: "+120.0 km", "-0:45", with an ASCII minus, which
  every font has.
- In the lines, the x axis is as wide as the longest line: a leap year
  two years back has 366 days.
- The heading of a month's weeks: "Cycling · distance by week · March
  2025"; of the lines: "Cycling · distance to date · 2025".

### The app (`SweatTrails.cpp`)

- Tasks `st-summaries-graph` (G), `st-summaries-drill` (Enter, and the
  chart's click) and `st-summaries-up` (Backspace). `st-summaries-close`
  (Esc) goes up, and closes the screen at the top.
- `on_week` opens the activity list as Ctrl+M does, at the activity's
  month, with it selected: `cmd_list_month`'s body becomes
  `list_month(sweattrails, id)`, which both use. The activity's
  `ActivityID` comes from its summary's file, `2025/03/<start>.fit`.

## Test plan

- `VolumeTest`: a month's weeks (March 2025 has 5 Mondays, February
  2026 4); `year_to_date()` adds up day by day, stops at `until`, is
  the whole year for a past one, and has 366 days in a leap year;
  `year_to_date_months()`'s totals at each month's end, and none after
  today.
- `BarChartTest`, renamed `ChartTest`, or a `LineChartTest`:
  `x_at()`/`y_at()`, a line shorter than the axis.
- `SummariesScreenTest`: G and back; drilling a year, a month and a
  week (the activity `on_week` gets, nothing for an empty week);
  Backspace and Esc back up, and Esc at the top reports nothing to pop;
  W after drilling clears the trail; Left and Right by month in a
  month's weeks, across New Year; the year-to-date heading and rows,
  with the signed difference and empty future months; no line for
  years before the first.
- `KeyBindingTest` has nothing new: the tasks are bound as 27's.
- By hand: the lines, the legend and the month ticks; a click on a bar
  drilling; the list opening at the week's first activity, and Esc back
  to the summaries.

## Review notes

*Filled in by the Reviewer.*

1. **Bug: the year-to-date table leaves the current month empty.**
   `year_to_date_months()` (`storage/Volume.cpp:284`) gives a month a
   total only if the values reach its last day, and this year's values
   stop at today. So on 15 June the June row has no total and no
   difference, and on 15 January the whole year's column is blank, with
   nothing to read "ahead by 120 km" off. The Architecture says only
   the months *after* this one are empty. The current month should
   show the total so far up to today. `test_line_stops_at_today`
   expects the empty June (`test/SummariesScreenTest.cpp:415`, "June
   isn't over"), so the fix has to change that check too. Failing
   test: `test_year_to_date_this_month`.
2. **Bug: Backspace after Left or Right in a month's weeks goes to the
   wrong year.** `up()` (`widget/Summaries.cpp:404`) takes the year
   from the trail, not from the month on display. Drill from 2025's
   months into March, press Left three times to December 2024, then
   Backspace: the screen shows 2025's months with nothing selected,
   because `drilled_into` (1 December 2024) isn't among them. It should
   show 2024's months with December selected. Failing test:
   `test_up_after_stepping_months`.
3. **Bug: Backspace after R or C deep down goes to a year outside the
   sport's years.** For the same reason as 2, and `up()` doesn't call
   `set_years()`. Running, Y, drill into 2021, then into March, then C
   (cycling starts in 2023): the weeks become March 2023, as
   `set_years()` clamps. Backspace then shows *2021*'s cycling months,
   all zeros, before `first_year`, with nothing selected, and the next
   Left jumps to 2023. Taking the year from the current view, as for 2,
   fixes both. Failing test: `test_up_after_changing_sport`.
4. **Bug (minor): this year's line is invisible on 1 January.**
   `LineChart::draw()` (`src/Lib/Widget/LineChart.cpp:69`) draws
   segments between values, so a series with one value draws nothing,
   and the legend names a year with no line. Drawing a dot for a
   one-value series would fix it. This can't be tested without a
   window.
5. **Suggestion: Right in a month's weeks goes into the months still to
   come.** `SummaryView::step_year()` (`widget/Summaries.cpp:157`)
   checks only the year. On 15 June 2026, Right from June's weeks goes
   to July through December 2026, all empty. Left also goes back to the
   months of `first_year` before the first activity. By year, Right
   stops at this year. Stopping at today's month would match.
6. **Suggestion: the year before the first activity gets a column of
   zeros.** That year has no line (Architecture), but the table's
   year-before column (`widget/Summaries.cpp:478`) still shows "0.0 km"
   for it, and "+" the whole total as the difference. Empty cells, as
   for the months after today, would match the lines.
7. **Suggestion: a stale comment in a test.** `test_lines`
   (`test/SummariesScreenTest.cpp:389`) says "no 2023 line" but checks
   for three series, 2025, 2024 and 2023. The check is right and the
   comment is wrong.

Tests added in review, uncommitted: in `test/SummariesScreenTest.cpp`,
`test_year_to_date_this_month`, `test_up_after_changing_sport` and
`test_up_after_stepping_months`, which fail (1, 3 and 2), and
`test_new_summaries_in_the_lines`, `test_new_summaries_in_a_month`,
`test_lines_in_a_leap_year`, `test_drill_into_a_week_across_months`,
`test_empty_store_lines_and_drilling` and `test_metric_in_the_lines`,
which pass. In `test/VolumeTest.cpp`,
`test_year_to_date_months_after_a_leap_year` and
`test_year_to_date_on_new_years_day`, which pass. All other tests pass
(`ctest -E oauth`); only `summaries_screen_test` fails.

### Resolution

1. Fixed: this month has the total up to today, against the year
   before's at the same date, the 28th of February for the 29th, in a
   row named "June to the 15th". `test_year_to_date_month_so_far` was
   added.
2. and 3. Fixed: going up keeps the year on display, which R and C keep
   in the sport's years, and selects the month on display.
4. Fixed: a line of one value is drawn as a dot; checked by reading, as
   drawing needs a window.
5. Fixed: in a month's weeks, Left and Right stop at the month of the
   sport's first activity and at this month (`volume_months()`).
   `test_step_by_month_stops_at_the_activities` was added.
6. Fixed: in the sport's first year, the year before's column and the
   difference are empty. `test_no_year_before_the_first` was added.
7. Fixed: the comment says what the test checks.

The Reviewer's tests are kept, and all pass.
