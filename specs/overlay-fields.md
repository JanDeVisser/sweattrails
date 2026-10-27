# Overlay fields

Status: In review

## Goal

The overlay can show everything the layout reference lists under
[Fields](overlay-layout.md#fields), not just speed, power, heart rate,
cadence, distance and elapsed time: pace for runs, altitude and grade for
climbs, the time of day to line the video up with the footage, the lap,
the activity's title and date. Values behave sensibly during a pause, and
a value can change color with how hard the effort is, such as power
turning red above threshold.

This is slice 3 of the [overlay roadmap](overlay-layout.md#roadmap). It
builds on [overlay-preview.md](overlay-preview.md), which has the layout
file, `text` and `value` elements, and the fields so far, and on
[video-overlay.md](video-overlay.md), which renders to video and
interpolates between records.

## Feature

### The new fields

All of them can be used in `text` templates, as `{field}`, `{field:spec}`
and `{field.unit}`, and as a `value` element's `field`. Until now a
layout could already name them, and they showed as missing. `title` and
`date` as a `value`'s field are odd, a text where a number with a unit
would be, but harmless, so they're allowed.

| Field         | Shows                                         | Metric    | Imperial  |
|---------------|-----------------------------------------------|-----------|-----------|
| `pace`        | time per distance, from `speed`               | min/km    | min/mi    |
| `altitude`    | `record.altitude`                             | m         | ft        |
| `grade`       | from altitude and distance                    | %         | %         |
| `temperature` | `record.temperature`                          | °C        | °F        |
| `lr_balance`  | left/right power, `48/52`                     |           |           |
| `time_of_day` | local time where the activity was             |           |           |
| `lap`         | the current lap, from 1                       |           |           |
| `title`       | the activity's title                          |           |           |
| `date`        | the activity's start date                     |           |           |

- **`pace`** is `m:ss` (`4:35`). It's missing below 1 km/h, which would
  otherwise show absurd values like `600:00`.
- **`grade`** is the change in altitude over the change in distance across
  a window of 30 m, centred on the moment: 15 m back and 15 m ahead, since
  the whole activity is there, and a window behind the moment lags, still
  climbing after the top. The reference had 100 m, but that averages a
  short gravel ramp of 18-20% with the road around it into 10%. With a
  barometric altimeter, about 0.2 m of noise, 30 m is good to about ±1%;
  with GPS altitude, 2-5 m of noise, it's much worse.
  - A `value` element can set its own window, `"window": 20`, from 10 to
    200 m, to tune it on one's own climbs in the preview. `{grade}` in a
    `text` template has the 30 m.
  - It's missing in the first and last 15 m, and without altitude or
    distance (indoors). However long the window takes to ride, a slow
    steep climb included, it counts; but a window with a pause in it
    (records more than 5 s apart) uses the riding on either side of the
    pause only, as if the pause weren't there: an altitude that changed
    while standing still (no more than 1.4 m ridden across the pause),
    such as a barometer drifting during a café stop, is no climb. During
    a pause it's the last value.
  - It's not `record.grade`, which most devices don't record, so a layout
    using it would be missing for most activities.
- **`lr_balance`**: FIT stores it as a bitfield. Bit `0x80` means the value
  is the right leg's share, bits `0x7f` the percentage, and `0xff` means
  there's none. It's shown as left/right, `48/52`. Without the `0x80` bit
  the side isn't known, and it's missing: guessing left would show a
  confident `52/48` that may be the wrong way round.
- **`time_of_day`** uses `Activity.utc_offset`, or the system's time zone
  if the file has none, as the render's timecode does.
- **`lap`** is the lap the moment is in, counted from 1. An activity
  without laps is lap 1 throughout.

### Formats

- Numbers take `std::format` specs, as now.
- `elapsed` takes `hms` (`1:05:09`, the default) or `ms` (`65:09`).
- `pace` takes `ms` (`4:35`, the default).
- `time_of_day` takes `hm` (`09:44`, the default) or `hms` (`09:44:10`).
- `date` takes `std::chrono` specs, and is `%d %b %Y` (`14 Jun 2025`) by
  default. Month and day names are English: `std::format` uses the C
  locale.
- A spec a field doesn't take is an error in the layout, which the preview
  shows, as a bad number spec is now.

### Pauses

Records more than 5 seconds apart mean the activity was paused (auto-pause,
or the timer stopped) or lost its signal. The records don't say which, so
every gap counts as a pause: in a tunnel, speed shows 0 until the signal
is back. Telling the two apart, from the distance growing across the gap,
would be another rule to explain and test, for a short and rare case.
Between two such records:

| Fields                                         | Shown as           |
|------------------------------------------------|--------------------|
| `speed`, `power`, `cadence`                    | 0                  |
| `pace`                                         | missing            |
| `heart_rate`, `temperature`, `lr_balance`      | missing            |
| `distance`, `altitude`, `grade`                | the last value     |
| `elapsed`, `time_of_day`                       | keep running       |
| `lap`, `title`, `date`                         | as they were       |

A field the activity doesn't have at all, such as power without a power
meter, is missing throughout.

### Colors by value

A `value` element can change color with its value:

```json
{ "type": "value", "field": "power",
  "color_by": { "thresholds": [[0, "#8ecae6"], [150, "#90be6d"], [220, "#f9c74f"], [260, "#f94144"]] } }
```

Each threshold is a value and the color from that value up; below the
first, the element has its own color. The number is compared as it's
shown: 259.6 W, shown as `260`, is at a threshold of 260. The thresholds are in the unit
shown: `40` is 40 km/h in a metric layout, or 40 mph if the element says
`"unit": "mph"`, since that's how whoever writes the layout thinks of it.
Switching the layout's `units` moves them, which matters little: they're
mostly for power and heart rate, which have no imperial units.

- Only the number changes color. The unit keeps its `unit_style`.
- A missing value has the element's own color.
- Only numbers have `color_by`, not durations such as pace, times,
  dates, the balance or the title.
- Only `value` elements have `color_by`. A `text` template can have
  several fields, so it's not clear which would pick the color. A
  `color_by` on a `text` is an error.

Zones from an athlete profile are *(deferred)*: there's no athlete
profile yet.

### Built-in layouts

- **`running-basic`** (new): pace, heart rate, cadence in spm, distance
  and elapsed time, laid out like `cycling-basic`. Ctrl+Shift+O picks it.
- **`cycling-basic`** gets `time_of_day`, small next to the elapsed time:
  it's what lines the video up with the footage while editing.
- The rest, altitude, grade, `color_by`, title and date, isn't in the
  built-in layouts, to keep them uncluttered; the README shows them.

### Version 1 scope

In: the fields above, their formats, the pause rules, `color_by`
thresholds, `grade`'s `window`, and the built-in layouts.

*(Deferred)*, as in the reference: `power_wkg`, `f.avg`, `f.max` and
`f.lap_avg`, `color_by` zones, `smooth`, `interpolate: false`, and
`max_gap` as a setting. Also the device's `record.grade`. And `color_by` on
pace, whose thresholds would need a syntax of their own (`"4:30"`).

## Open questions

None.

## Architecture

The fields stay in `overlay/Fields`, the layout's new properties in
`overlay/Layout`, and the drawing in `overlay/Renderer`. Nothing goes into
the Widget library: there are no new widgets.

### Fields and their kinds

- **`OVERLAYFIELDS`** gets the nine new fields, and `is_later_field()`
  goes, with the "can't be shown as a value yet" error: every field in the
  reference's table now exists. A name that isn't one, such as
  `power.avg`, is the usual "isn't a field".
- **`FieldKind`**, per field, says how it's formatted and which specs it
  takes:

  | Kind       | Fields                                                  | Specs                     |
  |------------|---------------------------------------------------------|---------------------------|
  | `Number`   | speed, power, heart_rate, cadence, distance, altitude, grade, temperature, lap | `std::format` |
  | `Duration` | elapsed, pace                                           | `hms`, `ms` (pace: `ms`)  |
  | `Clock`    | time_of_day                                             | `hm`, `hms`               |
  | `Date`     | date                                                    | `std::chrono` specs       |
  | `Balance`  | lr_balance                                              | `std::format`, both sides |
  | `Text`     | title                                                   | none                      |

  `is_valid_spec(field, spec)` checks a spec for the field's kind, where it
  only checked number specs; the template parser and `value_spec()` use
  it, and lose their special case for `elapsed`.
- **Values stay doubles** in `FieldValues`, in a base unit, so that
  interpolation works as it does: pace in s/m, altitude in m, grade in %,
  temperature in °C, `lr_balance` as the right leg's share, `time_of_day`
  in seconds since local midnight, `date` as local seconds since the
  epoch, `lap` from 1. `title` is the one text: `FieldValues` gets a
  `std::string title`.
- **`FieldUnit`** gets an `offset`, for °F (`× 1.8 + 32`).
- **`format_field()`** formats by kind: durations `h:mm:ss` or `m:ss`,
  clocks `HH:MM` or `HH:MM:SS`, dates with `std::chrono` from a
  `sys_seconds` that's already local, balance as `48/52`.

### An activity prepared for the overlay

- **`OverlaySource`** (`overlay/Fields.h`), made once from an `Activity`:
  the activity, its local offset (the `utc_offset`, or the system's time
  zone, which `activity_clock()` in `Render.cpp` then uses too, rather
  than working it out again), and the profile for `grade`: the records'
  `(distance, altitude)` where they have both, in order.
- `values_at_time()` and `values_at_record()` take an `OverlaySource`
  rather than an `Activity`. The preview keeps one for the activity it
  shows, and makes it again when the activity changes; a render makes one
  when it starts.
- **Pauses**: `values_at_time()` already knows when the two records around
  the moment are more than 5 s apart. It now applies the pause table
  there, rather than holding the earlier record's values.
- **`lap`** is the lap of the record before the moment, never
  interpolated. **`time_of_day`** and **`date`** come from the moment's
  time, not from records.
- **`grade_at(source, distance, window)`**: binary searches on the
  profile's distance for `distance ± window / 2`, interpolates the
  altitude at both, and divides. A pause adds no distance, so a window
  with a pause in it reaches past it by itself, and during a pause, with
  the distance held, the grade is held too. Missing at the ends, and
  without a profile. No per-window table is precomputed: it's two binary
  searches a frame.
- **`FieldValues::grade(window)`**, for a `value` element with a
  `window`: the frame's `FieldValues` keeps the source and the distance,
  and `values[grade]` is `grade(30)`.

### The layout

- **`ValueElement`** gets `std::optional<double> window` (10 to 200, only
  for `grade`: an error on another field) and
  `std::vector<Threshold> thresholds`, from `color_by`.
- **`color_by`** takes `thresholds`, a list of `[value, color]`, in
  increasing order, with `parse_color()`. Only for `Number` fields: a
  duration, a clock, a date, a balance or a title has no value to
  compare. Pace zones, with thresholds as `"4:30"`, are *(deferred)*.
  A `color_by` with `zones` says zones need an athlete profile,
  which there isn't yet. On a `text` element it's an error, as the
  Feature says.
- **`threshold_color(thresholds, shown, own)`**: the color of the last
  threshold at or below the shown value, else the element's own.
- **`shown_value(element, values, units)`**: what a `value` element shows,
  the number, its unit and the number's color, or nothing when it hides.
  It moved out of the renderer, so that a test can reach it; the renderer
  only measures and draws.

### Drawing

- The renderer colors the number by `threshold_color()`, and leaves the
  unit in `unit_style`.
- **Characters the layout can't know**: a title can have any character,
  `Rückweg`, which the fonts weren't loaded with, since
  `layout_codepoints()` only has the layout's own text. The title is the
  only such text (month and day names are English), so the renderer gives
  it to **`FontCache::add_codepoints()`** (Widget library), which adds
  the characters the fonts don't have, and unloads them to be loaded
  again with them. That happens once per activity at most.
  `layout_codepoints()` always has `°`, for `°C` and `°F`.

### Built-in layouts and the README

- `share/overlays/running-basic.overlay.json`, and `time_of_day` in
  `cycling-basic`.
- The README's overlay section lists the fields, formats and `color_by`.

## Test plan

- `OverlayFieldsTest`:
  - each new field from records: pace (and missing below 1 km/h),
    altitude, temperature, `lr_balance` (the right-leg bit, `0xff`, no
    bit), `lap` (with and without laps), `title`, `date`, `time_of_day`
    with and without `utc_offset`;
  - formats: `elapsed` in `hms` and `ms`, pace, clocks, dates, balance,
    °F, and the specs each kind refuses;
  - the pause table, field by field, in a gap;
  - `grade_at()`: a known climb, a short window keeping a steep ramp
    that 100 m flattens, a pause in the window, the ends, no altitude.
- `OverlayLayoutTest`: `window` (range, only on grade), `color_by`
  (thresholds, order, colors, zones, on a text, on a non-number field),
  `threshold_color()`, the units' codepoints, and fields that used to be
  "later" now decoding.
- `LayoutFileTest`: `running-basic` decodes, like `cycling-basic`.
- `OverlayRenderTest`: `activity_clock()` still passes on the shared
  offset.
- By hand: the preview of a ride with a climb, and of a run, with
  `running-basic`; a title with an accent; a render through a pause.

## Review notes

1. **Bug: an altitude step during a pause becomes a steep grade on a
   flat road, for the whole pause.** `OverlaySource::OverlaySource()`
   (`src/App/overlay/Fields.cpp:321-332`) puts the records before and after
   a pause into the profile as they are, and `grade_at()` (`:352-361`)
   interpolates straight across them. A barometric altimeter drifts during
   a long stop as the weather changes, and some devices recalibrate when
   they start again. So with 100 m altitude before a café stop and 104 m
   after it, on a flat road, every window that has the stop in it shows
   4 m over 30 m, 13.3%. Since the distance is held during the pause, the
   overlay shows 13.3% for the whole stop. The Feature says a window with
   a pause in it "uses the riding on either side of the pause only, as if
   the pause weren't there". Fix: when building the profile, if two
   consecutive records are more than `MaxGap` apart and the distance
   hardly grew, subtract the altitude step from every later point (a
   running offset). `grade_at()` only uses differences, so the offset does
   no harm. Test: `test_grade_with_an_altitude_step_in_a_pause` in
   `OverlayFieldsTest.cpp`, which fails (13.3 where it should be 0.0).
2. **Bug: `color_by` compares the unrounded value, so the color doesn't
   match the number shown.** `shown_value()` (`src/App/overlay/Layout.cpp:653`)
   passes `unit.shown(*number)`, before the spec rounds it. At 259.6 W
   the element shows `260`, but with a threshold at 260 it stays the
   color of the 220 threshold. A render interpolates between records, so
   this happens on many frames around every threshold, and on whole
   records in the preview, since `Segment::analyze` smooths power into
   fractions. The Feature says each threshold is "the color from that
   value up", in the unit shown. Fix: compare the number as formatted,
   for example by parsing the formatted text back with `std::from_chars`
   (it's a `Number` kind, so it parses), or by rounding to the spec's
   precision. Test: `test_threshold_at_the_shown_number` in
   `OverlayLayoutTest.cpp`, which fails.
3. **Suggestion: during a pause, a field the record before it is missing
   stays missing, not 0.** `paused()` (`src/App/overlay/Fields.cpp:137-141`)
   sets speed, power and cadence to 0 only if the earlier *record* has
   them. The spec's exception is for fields the *activity* doesn't have
   at all. With a power meter dropout on the last record before an
   auto-pause, power shows `--` for the whole pause, where the table says
   0. This is rare. A fix would check whether any record has the field
   (it could be in `OverlaySource`, worked out once).
4. **Suggestion: a render's title changes if the activity is renamed
   while it renders.** `OverlayRender` keeps `m_source` pointing at
   `m_job.activity->activity` (`src/App/overlay/OverlayRender.cpp:26`), and
   `values_at_time()` copies `activity->title` on every frame. F2 during a
   render renames that same `DisplayedActivity` in place, so the video's
   title changes halfway through, and the fonts are loaded again in the
   middle of the render if the new name has new characters. The pointers
   themselves are safe: the job's `shared_ptr` keeps the activity alive,
   and the preview gets a new `DisplayedActivity`, and so a new source,
   whenever `ActivityDisplay::set_activity()` runs. A fix is for the
   render to take its title when it starts (a `title` in `OverlaySource`).
5. **Suggestion: `running-basic` shows rpm for a walk or a hike.** The
   cadence element (`share/overlays/running-basic.overlay.json:41`) takes
   its unit from the sport, which is only spm for `sport::Running`. Picked
   for a walk, it shows strides as rpm, half the steps. The spec says
   "cadence in spm": `"unit": "spm"` on the element would make that so.
6. **Suggestion: `%Z` and `%z` in a `date` spec print `UTC` and `+0000`.**
   The date is local time stored in a `sys_seconds` (`Fields.cpp:77-80`,
   `:476-479`), so these specs are accepted but wrong for the place where
   the activity was. Either refuse them in `is_valid_spec()` for the
   `Date` kind, or say so in the README.

Tests added by the Reviewer, uncommitted:

- `OverlayFieldsTest.cpp`: `test_pause_at_record_times` (the record's own
  values at both ends of a gap, the pause table strictly between, the
  distance held across a tunnel, exactly `MaxGap` interpolated),
  `test_time_of_day_across_midnight`, `test_pace_between_records`
  (inverse of the interpolated speed, metric, imperial and explicit
  `min/mi`), `test_lap_at_its_change`, `test_balance_out_of_range`,
  `test_date_specs_format` (every date spec that's accepted formats
  without throwing, also for a date before 1970), `test_specs_per_kind`,
  `test_grade_with_duplicate_and_backward_distances`,
  `test_grade_with_gaps_in_the_records`, `test_grade_at_the_ends`, and
  `test_grade_with_an_altitude_step_in_a_pause` (fails: finding 1).
- `OverlayLayoutTest.cpp`: `test_thresholds_in_fahrenheit`,
  `test_threshold_at_the_shown_number` (fails: finding 2),
  `test_shown_grade_window`, `test_value_formats_per_kind`.

### Resolution

Jan agreed to all of them; each fix is its own commit.

1. Fixed: building the profile, an altitude step across a pause with no
   more than 1.4 m ridden (5 s at 1 km/h) comes off the points after it.
   A pause on a ramp with a few meters ridden keeps its climb.
   `test_grade_with_an_altitude_step_in_a_pause` passes.
2. Fixed: `shown_value()` compares the number its text reads back as,
   with padding and a sign stripped, else the value.
   `test_threshold_at_the_shown_number` passes, also padded and signed.
3. Fixed: `OverlaySource::recorded` says which fields the activity has;
   a pause shows 0 for those. `test_pause_after_a_dropout`.
4. Fixed: `OverlaySource::title` is taken when it's made; the preview
   makes its source again when the title changes. `test_title_when_made`.
5. Fixed: `"unit": "spm"` in `running-basic`.
6. Fixed: date specs with `%z`, `%Z`, `%Ez` or `%Oz` are refused.
   `test_date_without_zone`.
7. The Reviewer's tests are kept, all passing.
