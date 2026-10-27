# Overlay fields

Status: Draft

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
`max_gap` as a setting. Also the device's `record.grade`.

## Open questions

None.

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
