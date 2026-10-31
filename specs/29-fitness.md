# Fitness, fatigue and form (CTL, ATL, TSB)

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

The user sees how fit, how tired and how fresh they are, day by day, from
the training stress of everything they've done: the chronic training load
(CTL, "fitness"), the acute training load (ATL, "fatigue") and the
training stress balance (TSB, "form"), as TrainingPeaks' Performance
Management Chart shows them. They can see fitness build through a season,
fatigue after a hard block, and whether they're fresh for a race.

## Feature

### The method

These are the standard definitions, from Coggan and Allen's Performance
Management Chart, built on Banister's impulse-response model:

- Each activity has a **training stress score** (TSS): an hour at
  threshold is 100. A day's TSS is the sum of its activities'; a day
  without any has 0.
- **CTL** is an exponentially weighted average of the daily TSS with a
  time constant of 42 days, **ATL** the same with 7 days:
  `CTL(today) = CTL(yesterday) + (TSS(today) − CTL(yesterday)) × k`, with
  `k = 1 − e^(−1/42)` for CTL and `1 − e^(−1/7)` for ATL. Some tools use
  `k = 1/42` and `1/7`; the difference is under 2%.
- **TSB** is the form going into a day: yesterday's CTL minus yesterday's
  ATL. Positive is fresh, negative is tired.
- The **ramp rate** is how much CTL rose in the last 7 days.
- CTL and ATL start at 0 on the day of the first activity. With 17 years
  of activities, they've long settled by any year worth looking at.

### What there is now

- An activity's TSS comes from its power only:
  `TSS = moving seconds × NP × IF / (FTP × 3600) × 100`, with
  `IF = NP / FTP`. The FTP is a constant in the code, `JANS_FTP = 275`
  (`storage/Activity.h`); there's no setting.
- NP needs the records, so there's a TSS only when an activity is fully
  loaded, not in a month's list, and not in the summary file
  (`summaries.json`, `27-volume-summaries.md`), which the totals read
  from.
- Runs, and rides without a power meter, have no TSS. Their records
  usually have heart rate, and runs have pace.
- The summaries screen (Ctrl+T, `27-volume-summaries.md`,
  `28-volume-year-to-date.md`) has bars and lines of totals per period,
  which a fitness chart could sit next to.

### Training stress without power

The other common scores, which TrainingPeaks also uses:

- **hrTSS**, from heart rate: the time in heart-rate zones, set from the
  lactate threshold heart rate (LTHR). Good for steady efforts, low for
  intervals, as heart rate lags.
- **rTSS**, for runs, from pace: normalized graded pace, adjusted for
  the slope, against threshold pace. Poor on trails, where surface and
  terrain aren't in the pace.
- **TRIMP**, Banister's own, from duration and average heart rate
  against resting and maximum heart rate. On its own scale, not TSS's.

Each needs a threshold the app doesn't have: FTP for power, LTHR for
heart rate, threshold pace for rTSS.

### Where the training stress comes from

- An activity with **power** gets its TSS from power, as now, with the
  FTP in force on its day.
- An activity without power but with **heart rate** gets an **hrTSS**,
  with the LTHR in force on its day for its sport.
- An activity with neither counts as a rest day. Pace-based rTSS is left
  out *(deferred)*: it needs a threshold pace and grade-adjusted pace,
  for the few runs without heart rate.
- **hrTSS** is worked out as power's TSS is, with heart rate for power:
  second by second, `(HR / LTHR)²`, added up over the moving time, so
  that an hour at LTHR is 100, as an hour at FTP is. TrainingPeaks' own
  hrTSS is a lookup by zone, which it doesn't publish; this one is
  reproducible. Like any heart-rate score, it runs a little low for
  intervals, as heart rate lags.
- Running's LTHR scores every sport but cycling: a long hike is real
  fatigue, and running's is closest for being on one's feet.

### Scores that don't depend on the thresholds

A new threshold changes the TSS of every activity after its date, and
reading 1,800 FIT files again for each change would take minutes. Both
scores can be written as a number of the activity's own, divided by its
threshold squared:

- `TSS = moving seconds × NP² / (FTP² × 36)`
- `hrTSS = Σ (HR² × seconds) / (LTHR² × 36)`, over the moving records:
  the gaps between records of 10 seconds or less, as a dragged range's
  moving time leaves out pauses (`25-range-stats.md`).

So the summary file keeps each activity's NP and moving time (as it
does), and the sum of its heart rate squared over time, none of which
depend on a threshold, and the TSS is worked out from them when the
chart is drawn. Changing a threshold is instant. The summary file is
built again once, as NP and the heart rate need the records, which the
first build didn't read: that build takes longer than the first did.

### Thresholds

- The thresholds change over the years, so they're dated, in
  `settings.json`: from a date on, an FTP, and an LTHR for running and
  for cycling. Each activity is scored with the ones in force on its
  day.

  ```json
  "thresholds": [
    { "from": "2012-01-01", "ftp": 240, "lthr_running": 170, "lthr_cycling": 162 },
    { "from": "2024-03-01", "ftp": 275 }
  ]
  ```

  An entry that leaves a threshold out keeps the one before: above, the
  LTHRs of 2012 still count after March 2024. The first entry counts
  back to the first activity, so a list of one is simply today's
  thresholds for everything.
- They replace `JANS_FTP`: the data panel's IF and TSS use them too,
  and an activity with heart rate but no power shows its hrTSS there,
  "hrTSS 64", where a ride with power shows "TSS 87".
- Without any thresholds in `settings.json`, there's no TSS: the data
  panel shows no IF or TSS, and the fitness view says "Set your FTP and
  LTHR in settings.json to see your fitness", with the format. There's
  no default: 275 W is Jan's, and nobody else's.

### One fitness for all sports

- CTL, ATL and TSB are of all the activities' TSS together, as
  TrainingPeaks' chart is by default: fatigue doesn't care which sport
  made it. Per sport is left out *(deferred)*.

### On the summaries screen

- The fitness chart is a view of the summaries screen (Ctrl+T), next to
  the bars and the year-to-date lines. **F** shows it, and F again, G,
  or W, M and Y leave it for the bars or the lines.
- **The lines**: CTL bold, ATL and TSB, across the year shown, day by
  day; this year's stop at today. CTL and ATL decay through the days
  without activities, as they do. TSB goes below zero, so the line
  chart's scale runs from its lowest value to its highest, with the zero
  line marked, rather than from zero up as now.
- **The heading**: today's values, or the year's last day's for a past
  year, and the ramp rate: "Fitness · CTL 72 · ATL 85 · TSB -13 · +4.2
  a week · 2026". If the year has activities without power or heart
  rate, which count as rest days, it says how many: "· 3 without TSS".
- **The table**: a row per week of the year, the weeks whose Monday is
  in it as on the bars: the week's TSS, and CTL, ATL and TSB at its end.
  A week makes a hard block or a rest week stand out, which a month
  would hide.
- **Left and Right** go a year back or forward, from the year of the
  first activity to this one. R and C, and D, T, N and A, don't change
  it: the fitness is of all sports, in TSS.

### README

The README says how to set the thresholds, what CTL, ATL and TSB are,
and the F key.

### Version 1 scope

Everything above. Deferred:

- rTSS from pace, for runs without heart rate *(deferred)*.
- Fitness per sport *(deferred)*.
- Estimating FTP and LTHR from the activities *(deferred)*.
- Planned training, and the fitness it would lead to *(deferred)*.
- The device's own training load or TSS from the FIT file
  *(deferred)*: the app's own is the same for every device.

## Open questions

None.

## Architecture

### Where the spec leaves a gap

- **The current week's row** has the TSS so far, and CTL, ATL and TSB
  today. The weeks after today aren't in the table, and in a past year
  every week is.
- **"Without TSS"** counts the year's activities with a sport that got
  no score: no power or heart rate, or no threshold for what they have.
  Files that can't be read don't count, as they have no sport.
- **Bad thresholds in `settings.json`**, such as a `from` that isn't a
  date, or entries out of order, are said on the status line at start,
  and count as none. A threshold that is 0 or below is bad too.
- **A lap's and a range's TSS** in the data panel use the activity's
  sport and day, as its own does.
- **Enter and a click** do nothing in the fitness view, as in the lines.
  Up and Down go through the weeks.

### The scores (`storage/Thresholds.*`, `storage/Fitness.*`), pure

- **`Thresholds`**: `from`, a `year_month_day`, and optional `ftp`,
  `lthr_running` and `lthr_cycling`. **`decode_thresholds(JSONValue)`**
  reads `settings.thresholds` into a `std::vector<Thresholds>`, or says
  what's wrong with it. **`thresholds_on(list, day)`**: the thresholds in
  force on a day, each one carried forward from the entry that last set
  it, the first entry's counting back before its date.
- **`TrainingStress { value, heart_rate }`**, and
  **`training_stress(np, moving, hr_load, sport, thresholds)`**: power's
  `moving × NP² / (FTP² × 36)` if there's an NP and an FTP, else
  `hr_load / (LTHR² × 36)` with the sport's LTHR (cycling's for
  cycling, running's for the rest), else none. Taking the numbers, not a
  segment, lets the data panel and the summaries share it. These are in
  `Thresholds.*`, which the data panel uses, and what follows in
  `Fitness.*`.
- **`FitnessDay { tss, ctl, atl, tsb }`**, and
  **`fitness_days(summaries, thresholds, until)`**: a day each from the
  first activity's to `until`, by `start_day()` as for the volume.
  **`ramp_rate(days, ix)`**: CTL at `ix` less CTL a week before.
  **`fitness_weeks(days, year)`** gives the table's rows,
  **`without_stress(summaries, thresholds, year)`** the heading's count.

### Heart rate in the records (`storage/Activity.*`)

- `Segment` gets **`hr_load`**, `Σ HR² × seconds` over the gaps of
  `RangeMovingGap` (10 s) or less, worked out in `analyze()` as NP is.
  `intensity_factor` and `tss` go, and with them `JANS_FTP`: they depend
  on the thresholds, which the segment doesn't know.
- `Storage` gets **`thresholds`**, set once by the app after reading the
  settings, before any job runs, so that it can be read from any
  thread, as the paths can. The data panel reads them through
  `activity.storage`, so `activity_summary()` and the others keep their
  signatures. The data panel works out IF (power only), "TSS 87" or
  "hrTSS 64" with `training_stress()`.

### The summary file (`storage/Summaries.*`)

- `ActivitySummary` gets an optional **`np`** (W) and an optional
  **`hr_load`**. `SummariesVersion` goes to 2, so the file of version 1
  is read as none, and built again, as `read_summaries()` already does.
- `read_summary()` loads deeply, to get the records. It's no longer as
  quick: the job's chunks of 50 files still let other jobs in between.

### The chart (`src/Lib/Widget/Chart.*`, `LineChart.*`)

- `ChartScale` gets a **`bottom`**, 0 by default, and
  **`nice_range(min, max, ticks, min_step)`** a scale for a range that
  may go below 0: the bottom and the top on a step. (Not an overload of
  `nice_scale()`, whose `(max, ticks, min_step)` calls it would make
  ambiguous.) The bar chart
  stays from 0.
- `draw_chart_scale()` draws from the bottom; the line at 0 is already
  marked brighter than the others. `LineChart::scale()` takes the
  lowest value as well, so lines that go below 0 get a scale that does,
  and the year-to-date lines are as they are. `y_at()` uses the bottom.

### The screen (`widget/Summaries.*`)

- `SummaryView::lines` becomes **`graph`**, an enum of `Bars`,
  `YearToDate` and `Fitness`, as there are now three.
- `SummariesScreen` gets `thresholds`, and **`toggle_fitness()`** (F):
  the fitness view at the top, or back to the bars. G from the fitness
  view goes to the lines, W, M and Y to the bars. R, C, D, T, N and A
  change the view underneath, which shows when the user leaves.
- The fitness view uses the line chart, three series: CTL bold, ATL,
  TSB, and the table with `fitness_columns()` and `fitness_rows()`, pure
  as the others. **`fitness_heading()`**: "Fitness · CTL 72 · ATL 85 ·
  TSB -13 · +4.2 a week · 2026 · 3 without TSS", or the message about
  settings.json without thresholds, when the chart and the table are
  empty.
- Left and Right go between the year of the first activity of any
  sport and this year (`SummaryView::fitness_years`).

### The app (`SweatTrails.cpp`)

- `read_settings()`'s caller decodes the thresholds into
  `storage.thresholds`, and sets the status if they're bad.
- Task `st-summaries-fitness`, F, `.when(on_summaries)`.

## Test plan

- `FitnessTest`: `decode_thresholds()`, good and bad; `thresholds_on()`
  before the first entry, between them, carrying a threshold forward;
  `training_stress()`: an hour at FTP is 100, an hour at LTHR is 100,
  power before heart rate, cycling's LTHR for a ride and running's for a
  hike, none without the threshold; `fitness_days()`: one activity's
  CTL and ATL against the formula, the decay through rest days, TSB as
  yesterday's, two activities on a day adding up; `ramp_rate()`;
  `fitness_weeks()` up to today; `without_stress()`.
- `ActivityTest` (or `DataFieldsTest`): `hr_load` over records with a
  gap longer than 10 s; the summary's "TSS 44" with an FTP of 275 in
  the storage's thresholds, "hrTSS" without power, nothing without
  thresholds.
- `SummariesTest`: version 2 round trip with `np` and `hr_load`; a file
  of version 1 is read as none; `read_summary()` of a FIT file with
  power and heart rate.
- `ChartTest`/`LineChartTest`: `nice_scale(-23, 85)`; `y_at()` with a
  bottom below 0; the year-to-date scale still from 0.
- `SummariesScreenTest`: F and back, G and W from the fitness view; the
  heading's values, and the message without thresholds; Left and Right
  by year; R doesn't change the fitness; the weeks' rows.
- By hand: the chart below zero with its zero line, and the heading on
  Jan's activities, after the summary file is built again.

## Review notes

*Filled in by the Reviewer.*
