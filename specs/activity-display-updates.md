# Cadence, a Summary Under the Title, and a Smoothed Climb

Status: In progress, on branch feature/activity-display-updates

## Goal

Three additions to the activity display, after
[activity-data-fields.md](activity-data-fields.md):

- Cadence in the data panel, with its maximum and minimum clickable, as HR
  and power are.
- The activity's distance, active time and TSS, big enough to read at a
  glance, near the title.
- A meters-climbed figure that isn't inflated by noise in the altitude.

## How it works now

- `Segment::analyze()` computes `computed_cadence_range`, a 3-sample
  rolling average like HR and power, but the panel doesn't show it.
  The average includes zeros, such as from coasting or standing still.
- The FIT `record` has `cadence` (`u8`) but not `fractional_cadence`. In
  FIT, running cadence is in strides per minute: one foot's steps. Garmin
  Connect and Strava show it doubled, as steps per minute.
- The header in `ActivityDataDisplay::draw()` shows the sport icon, the
  sport and start time in a small font, and the title in the large font.
  Below it, the panel's first lines are Moving Time | Elapsed Time and
  Distance.
- `Segment::analyze()` computes `tss`, but nothing shows it. It uses the
  elapsed time, stops included, where TSS is normally worked out over the
  moving time. IF uses the hard-coded FTP of 275 W (`JANS_FTP`).
- `computed_climb` is the sum of every rise in altitude between records.
  GPS altitude jitters by a meter or so, and every upward jitter counts, so
  it reads high.
- The FIT session message has a `total_ascent`, the device's own climb,
  but the loader doesn't read it.

## Feature

### Cadence

- A line Avg Cadence | Max Cadence, and a line Min Cadence, after the
  power lines. Shown when the activity has cadence (a maximum of at least
  10, as for HR and power).
- Rides in rpm, all other sports in steps per minute (spm), which is the
  FIT cadence doubled.
- Max Cadence and Min Cadence can be clicked, and mark the record where
  they happened, as Max HR does.
- Min Cadence is the lowest 3-sample average whose samples are all
  non-zero, and that isn't within 5 records of a zero, before or after. A
  zero is coasting or standing still, and the records around it are
  slowing down to it or getting going again, which isn't the cadence
  anyone rode or ran at.
- Avg Cadence leaves out the zeros, as Garmin shows it by default: it's
  the average while pedaling or running, not pulled down by coasting.

### Summary under the title

- Under the title, in the title's font: the distance, the active time
  (the timer time, which the panel calls Moving Time) and the TSS, as
  `42.3 km · 1:32:10 · TSS 87`. Under rather than next to the title,
  because titles are often long, and the panel is only about two thirds of
  the window wide.
- Parts without data are left out: no distance, or no TSS without power.
- TSS is worked out over the moving time, not the elapsed time, and still
  uses the hard-coded FTP.
- The panel no longer shows Distance and Moving Time, so no number is
  shown twice. Its first line is Elapsed Time; the speed or pace line
  follows as now.

### Smoothed climb

- When the FIT file has a `total_ascent`, the panel shows that: it's the
  number the device and Garmin Connect show, from the device's own
  filtering, which often uses a barometer.
- Otherwise the climb is worked out with hysteresis: a rise counts only
  once the altitude is at least 3 m above the lowest point since the last
  counted rise. Jitter smaller than that adds nothing; on a real climb at
  most the last 3 m short of the top go uncounted.
- Laps keep working out their own climb, with the hysteresis.

### Version 1 scope

Everything above. Deferred, as before: an FTP setting *(deferred)*.

## Open questions

None.

## Architecture

**The device's climb.** The FIT `session` struct gets
`std::optional<u16> total_ascent` (FIT field 22, in meters), added to
`session_meta` and the type list of `make_from_rec<session>`. The session
handler in `Activity::load_fit_file_filter()` copies it to a new
`Segment::total_ascent`. Laps don't get one, so they use the computed
climb, as the spec says.

`Segment::climb()` returns the device's `total_ascent` if there is one,
and `computed_climb` otherwise. The panel shows `climb()`.

**Hysteresis.** `Segment::analyze()` keeps a reference altitude: the
lowest altitude since the last counted rise. When an altitude is at least
`ClimbHysteresis` (3 m, in `Activity.h`) above it, the difference is added
to `computed_climb`, and the reference moves up to that altitude. A lower
altitude moves the reference down.

**Cadence.** `computed_cadence_range` stays as it is: it smooths the
records' cadence, as for HR and power, and gives Max Cadence and its
`max_at`. Min and average need the raw samples, with the zeros, so
`analyze()` collects them (before it smooths the records) and hands them
to a free function, in `storage/Activity.h`:

```cpp
struct CadenceSample {
    size_t at;      // the record's index
    u8     cadence; // as in the FIT file: rpm, or strides per minute
};

struct CadenceStats {
    float32               average { 0 }; // without the zeros
    std::optional<u8>     min { };       // see the spec for which windows count
    size_t                min_at { 0 };
};

std::optional<CadenceStats> cadence_stats(std::span<CadenceSample const> samples);
```

It returns nothing without a non-zero sample. `min` is the lowest average
of 3 consecutive samples that are all non-zero, none of them within 5
records of a zero sample; there may be no such window, and then there's no
Min Cadence. `min_at` is the record that completes the window, as for the
smoothed ranges. The result goes in a new `Segment::cadence`.

`data_fields()` doubles the cadence for everything that isn't a ride, and
says "rpm" or "spm".

**TSS** in `analyze()` uses `moving.elapsed` instead of `elapsed.elapsed`.

**The summary line.** `DataFields.h` gets

```cpp
// The line under the title: distance, active time and TSS, as far as the
// activity has them, e.g. "42.3 km · 1:32:10 · TSS 87".
std::string activity_summary(Activity const &activity);
```

with the formats of `data_fields()` for the distance and the time. TSS is
shown under the same conditions as NP: power with a realistic maximum, and
an NP. `ActivityDataDisplay::draw()` renders it in the title's font, under
the title, and moves the fields down by its height. "·" is U+00B7, which
VictorMono, the default font, has.

**The panel's first line** is Elapsed Time on its own; Distance and Moving
Time move to the summary.

## Test plan

In `test/DataFieldsTest.cpp`:

- `Segment::analyze()`'s climb with hysteresis: jitter below 3 m adds
  nothing; a steady climb counts all but at most the last 3 m; a climb, a
  descent and another climb; records without altitude are skipped.
- `Segment::climb()` prefers `total_ascent`.
- `cadence_stats()`: the average leaves out zeros; the minimum skips
  windows with a zero, and windows within 5 records of one, on both sides;
  no minimum when no window is left; nothing without non-zero samples;
  `min_at`.
- `data_fields()`: cadence in rpm for rides and doubled in spm for runs,
  Max and Min Cadence clickable with their records, no cadence lines below
  the realistic maximum; the first line is only Elapsed Time.
- TSS over the moving time.
- `activity_summary()`: all three parts; without distance; without TSS
  (no power, or too few samples for NP).

Loading `total_ascent` from a FIT file isn't tested: there's no FIT test
data in the repository. Checked by hand on a real activity, against the
climb Garmin Connect shows. Drawing the summary is checked by hand too.

## Review notes

*Filled in by the Reviewer.*
