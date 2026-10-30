# Volume summaries: year to date, and drilling down

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

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
