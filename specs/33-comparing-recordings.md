# Comparing the recordings of an activity

Status: In progress, on branch feature/33-comparing-recordings

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

The user records a ride on two devices at once, each with its own power
meter, to compare the power meters (`31-multiple-recordings.md`). With
Tab (`32-switching-recordings.md`) they can flip between the recordings,
but that only shows one at a time. They want to see the two together:
both powers on the graph, and how far apart they are, over the whole
ride, per lap, and over a stretch they pick.

This is the last of the three specs in `31-multiple-recordings.md`.

## Feature

### What there is now

- An activity recorded on several devices is one activity, with a main
  recording and others in a folder. Tab and Shift+Tab switch between
  them, keeping the map's view and the selected lap. The summary line
  says "Recording 2 of 2: Wahoo".
- The graph (`ActivityGraph`) draws altitude, speed, heart rate and
  power of one activity into an image. Its x-axis is the record: a
  lap is as wide as its share of the records. Two devices record at
  different rates, and start at different times, so their records don't
  line up: comparing has to go by the clock.
- The data panel shows the activity's, the lap's or a dragged range's
  fields, among them average and maximum power and NP. The lap table
  has a row per lap.
- `src/Lib/Widget` has a `LineChart`, used by the fitness chart.

### Comparing, on and off

- **Ctrl+K** turns comparing on and off, on the activity screen. On an
  activity with one recording, turning it on says "This activity has
  one recording", and nothing changes.
- It **stays on** across Tab, Left and Right and Ctrl+M, until Ctrl+K
  again: going through a week of compare rides takes one key. An
  activity with one recording shows as usual while it's on.
- The summary line says what's compared with what, where it already
  says which recording is shown: "… · Recording 2 of 2: Wahoo, compared
  with Garmin". The lap's and range's lines say so too.

### Which two

- The recording on display is compared with the **main one**; on the
  main one, with recording 2. Tab while comparing changes the one on
  display, so with three recordings or more the user goes through each
  against the main one.

### What's compared

- **Power, cadence and heart rate.** Power is why the user records
  twice; cadence comes from the power meters too, and explains some
  power gaps; and the user has several heart rate sensors (a Wahoo
  armband, a Garmin chest strap, the optical sensor in a Forerunner) to
  compare as well.
- **By the clock, second by second**: the two recordings' records are
  matched by their time, and a second only counts when both have a
  value for it. A head unit started a minute late, or a dropout on one
  device, doesn't skew the difference. The devices' clocks come from
  GPS and agree within a second; nudging one by an offset is
  *(deferred)*.

### The graph

- While comparing, the graph shows **power, heart rate and cadence of
  both** recordings: the one on display's in its colours, the other's in
  a lighter shade of the same colour, on the same scale. Altitude and
  speed are left out while comparing, so that six lines stay readable.
  Cadence isn't drawn now: comparing adds it.
  The x-axis stays the one on display's records, and the other's values
  are drawn at the records' times.
- The banner under the cursor gives both: "Pwr 245 / 238 W · HR 142 /
  145 · Cad 88 / 87", the one on display's first.
- Selecting a lap, dragging a range and the map work as they do now, on
  the recording on display.

### The numbers

- The **data panel** shows, for what's selected, the activity, a lap or
  a dragged range, over the seconds both recorded: average and maximum
  power, NP, average cadence, and average and maximum heart rate. Each
  as this one, the other one and the difference: "245 / 238 W · +7 W
  (+3%)". A value one of them doesn't have shows as "–", and has no
  difference. These take the place of the panel's own power, cadence
  and heart rate fields while comparing; the other fields, distance,
  times, speed and elevation, stay as they are, the one on display's.
- A lap is the recording on display's lap; the other's numbers are over
  the same clock times.
- The **lap table** gets one column, the power difference per lap,
  "+7 W", while comparing.
- **The sign**: the one on display minus the other, the percentage of
  the other's. "+7 W (+3%)" means the one on display reads 7 W higher.
  Tab flips the sign, as it's always seen from the one on display.

### README

The README's section on activities recorded on several devices says
that Ctrl+K compares the recording on display with the main one, what
the graph, the panel and the lap table show, and which way round the
difference is.

### Version 1 scope

Everything above. Deferred:

- Nudging one recording's clock by an offset *(deferred)*.
- Comparing speed, distance and ascent *(deferred)*.
- Comparing more than two at once *(deferred)*.
- A report over many activities, such as how two power meters differ
  on average over a season *(deferred)*.

## Open questions

None.

## Architecture

### Pairing and the numbers (`storage/Comparison.*`, new)

Pure functions on records, so that they're tested without a window.

- **`pair_records(shown, other)`**: for each record of the recording on
  display, the index of the other's record at its time, or none. "At its
  time" is the other's record at the same second, or the last one before
  it if that's at most `PairHoldSeconds` (2 s) earlier, as a FIT record's
  values hold until the next one: smart recording writes a record every
  few seconds. One pass over both, as both are in time order. The pairs
  index both `records` (smoothed, for the graph) and `recorded` (as
  recorded, for the numbers), which have the same records.
- **`Compared`**: one number of both, `std::optional<float64> shown,
  other`, and `difference()` and `percent()` when both are there.
- **`Comparison`**: average and maximum power, NP, average cadence, and
  average and maximum heart rate, each a `Compared`.
- **`compare(shown, other, pairs, first, end)`**: the `Comparison` over
  the shown records `[first, end)`, from `recorded`. Each number goes
  over the records where both have that value: power over those where
  both have power, and so on. Cadence leaves out the seconds where
  either is 0, as the activity's own cadence does. NP is a
  `NormalizedRange<u16, 30>` of each over the paired seconds, as the
  segments' NP is, and none under 30 of them.

### The recording compared with (`widget/DisplayedActivity.*`)

- **`DisplayedActivity::compared`**: `std::optional<ComparedRecording>`,
  the other recording (`Activity`), its device's name, and the pairs.
  Set while comparing; none otherwise.
- **`DisplayedActivity::comparison()`**: the `Comparison` of what's
  selected: the lap, the dragged range, or the whole activity.

### The screen

- **`SweatTrails::comparing`**, on the UI thread, toggled by
  `st-compare-recordings` (Ctrl+K, `.when(on_activity_screen)`). Turning
  it on for an activity of one recording says so, and leaves it off.
- **Loading the other**: `show_activity()` and `show_recording()` call
  `compare_if_on()`, which, while comparing and for an activity of more
  than one recording, loads the other one (the main one, or recording 2
  on the main one, from `recordings_in_order()`) with a deep load, and
  sets it on the display with `ActivityDisplay::compare_with()`. Ctrl+K
  does it for the activity on display, or clears it. Loading on the UI
  thread is as Ctrl+M's list and Left and Right do it.
- **The data panel**: `data_fields(activity, segment, first_record,
  comparison)` takes an optional `Comparison`; with one, its lines take
  the place of the HR, power and cadence lines: "Avg Power  245 / 238 W
  · +7 W (+3%)". `format_compared()` formats one.
- **The summary lines**: `compared_suffix(device)` gives ", compared
  with Garmin", which the panel adds after the activity's, lap's or
  range's line.
- **The lap table**: `lap_table(activity, lap_comparisons)`, with the
  laps' `Comparison`s while comparing, adds a "Δ Power" column, "+7 W".
- **The graph**: `ActivityGraph` draws, while comparing, power, heart
  rate and cadence of both, the other's from the pairs in a lighter
  shade (`ColorBrightness`), on one scale for both, and leaves out
  altitude and speed. The banner's text comes from
  `compare_banner(record, other_record)`, in `widget/DataFields.*`, so
  it's tested.

## Test plan

- `ComparisonTest` (new):
  - `pair_records()`: two 1 s recordings started a minute apart; one
    recording every 3 s against one every second; a gap in the other of
    more than 2 s; nothing in common.
  - `compare()`: averages and maxima over the paired seconds only, with a
    dropout on one; cadence leaving out zeros; NP with 30 paired seconds
    or more, and none under; a value only one has; `difference()` and
    `percent()`, and their sign.
- `DataFieldsTest`: `format_compared()` with both and with one;
  `data_fields()` with a comparison replacing the HR, power and cadence
  lines; `compared_suffix()`; `lap_table()`'s "Δ Power" column;
  `compare_banner()`.
- `DisplayedActivityTest`: `comparison()` of the activity, a lap and a
  range.
- By hand: Ctrl+K on a compare ride, the graph's six lines and the
  banner, a lap, a range, Tab with comparing on, Left and Right to an
  activity of one recording and back, and Ctrl+K on an activity of one
  recording.

## Review notes

*Filled in by the Reviewer.*
