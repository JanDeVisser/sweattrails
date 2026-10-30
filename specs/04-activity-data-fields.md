# More Data Fields in the Activity Display

Status: Done

## Goal

The data panel next to the map shows time, distance, pace and elevation,
but not heart rate or power, and it shows a pace even for rides, where
speed is what riders think in. With this feature the panel shows the
numbers a cyclist or runner looks at first. Clicking a maximum shows where
in the activity it happened.

## How it works now

- `ActivityDataDisplay::draw()` in `src/App/widget/Activity.cpp` renders
  fixed lines of text in two columns: moving and elapsed time, distance,
  average pace (min/km, always), and min and max elevation.
- `Segment::analyze()` already computes everything this feature needs:
  - `computed_hr_range`, `computed_power_range`: average, max and the
    index of the max (`max_at`), over a rolling 3-sample average. The
    records themselves are overwritten with the smoothed values, so the
    graph shows the same smoothed curves.
  - `computed_speed_range`: average, max and `max_at`, not smoothed.
  - `computed_normalized_range`: normalized power (NP).
  - **Catch:** `max_at` counts only the samples that have that value, not
    records. When some records have no HR or power, as during sensor
    dropouts, `max_at` doesn't match the record index. It has to become a
    record index for this feature.
  - `intensity_factor` (IF) and `variability_index` (VI). IF uses the
    hard-coded FTP `JANS_FTP` = 275 W.
- `DisplayedActivity::mark` is a record index. The graph draws it as a
  vertical line with a banner of the values there, the map as a black dot.
  Hovering over the graph sets it.
- Nothing in the data panel can be clicked. The text is drawn with
  `render_text()`, with no widgets per field.

## Feature

### Fields

The panel keeps its two columns. A field whose data isn't in the activity
isn't shown, and nor is the line it's on if both of its fields are missing.

| Left                    | Right                    | Shown when                 |
|-------------------------|--------------------------|----------------------------|
| Moving Time             | Elapsed Time             | always                     |
| Distance                |                          | distance > 0               |
| Avg Pace / Avg Speed    | Max Pace / Max Speed     | distance > 0               |
| Avg HR                  | Max HR                   | the activity has HR        |
| Avg Power               | Max Power                | the activity has power     |
| NP                      | IF                       | the activity has power     |
| VI                      |                          | the activity has power     |
| Min elevation           | Max elevation            | the activity has altitude  |
| Climbed                 |                          | the activity has altitude  |

- Rides (`sport::Cycling`, with any sub-sport such as indoor or e-bike,
  and `sport::E_biking`, which FIT has as a sport of its own) show speed
  in km/h, with one decimal. All other sports show pace in
  min/km. The fastest pace is the max speed converted to pace.
- Average speed is distance over moving time, as average pace is now.
- HR is in bpm, power in W, both as whole numbers. IF and VI have two
  decimals.
- Climbed is the meters climbed, as a whole number: every rise in altitude
  from one record to the next counts, and descents don't take anything
  off.
- An activity "has HR" or "has power" only when its maximum is at least 10
  bpm or W. A lower maximum isn't realistic, and then no HR, or no power,
  NP, IF or VI, fields are shown.

### Showing where a maximum happened

Max HR, Max Power, Max Speed / Max Pace, and Min and Max elevation can be
clicked, on the label or the value. Over them, the mouse cursor becomes a
pointing hand.

Clicking one sets the existing mark (`DisplayedActivity::mark`) to the
record where the maximum (or, for Min elevation, the minimum) happened. The graph draws its line and value
banner there, and the map its dot, as they do for hovering now. There's no
separate marker: hovering over the graph moves the mark again, as it does
now. That was chosen over a sticky marker of its own because it's simple,
with the drawback that moving the mouse across the graph replaces the
clicked maximum.

- The values shown are the ones the graph shows: HR and power maxima are
  the maxima of the 3-sample rolling average, not the raw peaks. That way
  the panel, the graph and the banner at the mark agree. A short spike
  reads a little lower than on a head unit or Strava.
- IF uses the hard-coded FTP of 275 W (`JANS_FTP`), as now. An FTP setting
  is *(deferred)* to a spec of its own.

### Version 1 scope

Everything above, except an FTP setting *(deferred)*. TSS isn't shown:
it isn't asked for, and it also depends on FTP.

## Open questions

None.

## Architecture

**`max_at` becomes a record index.** `SmoothedRange::extend()` takes the
index of the record the value comes from, `extend(value, at)`, and
`min_at` and `max_at` store that instead of counting samples.
`Segment::analyze()` passes the index into the records it analyzes. For a
lap that is the index within the lap; the activity's own segment, which is
what the panel shows, analyzes all records, so there it's the record
index. For a smoothed range, it's the record where the smoothed value is
written, which is where the graph shows the maximum.

`max_at` now points at the first record with the maximum, not the last:
`extend()` updated it on ties. "Where the maximum happened" reads as the
first time it was reached. The same goes for `min_at`.

**The fields are worked out apart from drawing them**, in a new
`src/App/widget/DataFields.h` and `.cpp`, with no raylib:

```cpp
struct DataField {
    std::string           label;   // "Max HR"
    std::string           value;   // "172 bpm"
    std::optional<size_t> mark_at; // the record to mark when it's clicked
};

struct DataLine {
    std::optional<DataField> left;
    std::optional<DataField> right;
};

// The lines of the data panel, without the ones with no fields.
std::vector<DataLine> data_fields(Activity const &activity);
```

All the rules in the Fields table, the units and the decimals are in
`data_fields()`, formatted with `std::format`. The moving time, elapsed
time and distance keep the formats they have now.

**`ActivityDataDisplay` only draws the lines and handles the mouse.** Its
`draw()` renders each field's label, as `"<label>:"`, and its value
separately, so the values line up in any font: in each column, the values
start at the same x, past the widest label in that column, measured with
the font. (Now the labels are padded with spaces, which only lines up in a
monospace font, such as the default VictorMono, and is off by one for
Elapsed Time.) It keeps the screen rectangle of each field with a
`mark_at`, from the start of its label to the end of its value. Its
`process_input()` checks the mouse against those rectangles: over one, it
asks for the pointing hand cursor; on a click, it sets the mark with
`DisplayedActivity::set_mark()`. The rectangles are from the previous
frame's `draw()`, because that's where the text is measured.

**The cursor goes through the app (in `src/Lib/Widget`).** The app sets an
I-beam cursor at startup, and nothing changes it. If the panel set the
cursor itself, it would also have to put it back, and it can't always: while
a modal is open, as after Ctrl-M with the mouse over a maximum, only the
modal gets `process_input()`. So:

```cpp
// App.h
// A widget that wants another cursor than the default asks for it in
// process_input(), in every frame it wants it.
void request_cursor(MouseCursor cursor);
```

`App::process_input()` resets the request to the default cursor (the
I-beam) before the widgets handle input, and afterwards calls
`SetMouseCursor()` if the request differs from the cursor that is shown.

**Climbed** is worked out in `Segment::analyze()`, as
`Segment::computed_climb`: the sum of the rises in altitude between
consecutive records with an altitude. A record without altitude is skipped,
so the climb goes on from the altitude before it. Laps get their own climb,
as they get their own ranges.

**When there's no data for a field.** "The activity has HR" (or power)
means that its maximum is at least 10 (bpm or W). Below that it can't be
real, such as a sensor that only sent zeros, and none of the HR fields, or
none of the power fields, NP, IF and VI included, are shown. NP, IF and VI are only shown when there is an
NP, which needs more than 30 power samples. VI is only shown when the
average power is above 0. Max Speed / Max Pace needs speed samples.

## Test plan

A new `test/DataFieldsTest.cpp` (`data_fields_test`), built with the
storage sources, as `activity_selector_test` is:

- `SmoothedRange`: `max_at` and `min_at` are the indices passed to
  `extend()`, also when records are skipped, and point at the first record
  with the maximum on ties. With smoothing, `max_at` is the record that
  completes the window with the highest average.
- `Segment::analyze()`: records with gaps in their HR and power, as in a
  sensor dropout. `max_at` is the record index of the maximum. The climb
  only counts rises, and skips records without altitude.
- `data_fields()`, for activities built in the test:
  - a ride shows speed in km/h with one decimal, a run pace in min/km,
    and Max Pace is the max speed as a pace;
  - HR, power, NP, IF and VI with their units and decimals;
  - fields and lines without data aren't there: no HR, no power, no
    altitude, no distance, an HR or power maximum below 10, too few power
    samples for NP;
  - the maxima, and Min elevation, carry the `mark_at` of their record,
    and the other fields none.

Not tested automatically, because they need a window and fonts: drawing
the panel, the hit rectangles, the cursor, and the click. Checked by hand:
click each maximum on a ride and a run, and check the graph's line and the
map's dot; check that the cursor is a pointing hand over a maximum only,
and that it's reset after Ctrl-M with the mouse over one.

## Review notes

I found nothing wrong with the record-index `max_at`, the rules for which
fields show, the units or the cursor handling. The two findings are small,
and one is about the spec rather than the code.

1. **Bug (minor):** `format_pace()` in `src/App/widget/DataFields.cpp:58`
   works out the minutes as `pace.hours * 60 + pace.minutes` and drops
   `Duration::days`. A pace of a day or more per km comes out too low. An
   example is a gym or indoor session where the watch logged a few meters:
   5 m in 50:00 is 10000:00 min/km, but the panel shows "1360:00 min/km".
   The same goes for Max Pace when the max speed is below about 0.0116 m/s.
   `test_pace_of_more_than_a_day` in `test/DataFieldsTest.cpp` fails on
   this. The fix is to use `pace.elapsed / 60` and `pace.elapsed % 60`.
2. **Suggestion:** Only `sport::Cycling` counts as a ride
   (`DataFields.cpp:69`), as the spec says. But FIT also has a separate
   sport `E_biking` (21), apart from Cycling's e-bike sub-sports. An
   activity recorded with that sport shows a pace in min/km. Jan should
   decide whether `E_biking` counts as a ride too. If it does, the spec and
   the check both need to change.
3. **Missing tests (added):** in `test/DataFieldsTest.cpp`:
   - `max_at` with gaps in power, not just HR, as the Test plan asks;
   - an HR or power maximum of exactly 10 is shown;
   - VI is hidden when the average power rounds down to 0 W but there is
     an NP, while NP and IF are still shown;
   - with a distance but no moving time and no speed, there's no pace line;
   - Max Pace and its `mark_at` with records that have no speed;
   - the elevation values, which can't be clicked.

   A new `test/CursorTest.cpp` (`cursor_test`) checks
   `App::request_cursor()`. A request lasts for one frame, and while a
   modal is open, a request left over from the panel goes back to the
   I-beam. `SetMouseCursor()` turns out to be safe to call without a
   window. These tests pass.

### Resolution

1. Fixed in 5c39738: the pace is worked out from `Duration::elapsed`.
   `test_pace_of_more_than_a_day` passes.
2. Jan decided that `E_biking` counts as a ride. Fixed in 229e00b, in the
   spec's Feature section and in `data_fields()`, with
   `test_e_bike_ride_shows_speed`, which fails without the change.
3. The tests are kept.
