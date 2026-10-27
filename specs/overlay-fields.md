# Overlay fields

Status: In progress

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
    pause only, as if the pause weren't there. During a pause it's the
    last value.
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
first, the element has its own color. The thresholds are in the unit
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

### Drawing

- The renderer colors the number by `threshold_color()`, and leaves the
  unit in `unit_style`.
- **Characters the layout can't know**: a title can have any character,
  `Rückweg`, which the fonts weren't loaded with, since
  `layout_codepoints()` only has the layout's own text. The renderer looks
  at the text it's about to draw, and if it has characters the fonts
  don't, adds them and loads the fonts again. That happens once per new
  character, so once per activity at most. `layout_codepoints()` adds the
  default units' characters, `°C` and `°F`.

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

*Filled in by the Reviewer.*
