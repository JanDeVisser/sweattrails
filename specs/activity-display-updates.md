# Cadence, a Summary Under the Title, and a Smoothed Climb

Status: Done

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
- Rides in rpm. Running, walking and hiking in steps per minute (spm),
  which is the FIT cadence doubled. Other sports, such as swimming or
  rowing, as FIT records them, in strokes per minute, shown as "/min".
- Max Cadence and Min Cadence can be clicked, and mark the record where
  they happened, as Max HR does.
- A record counts as pedaling (or running) when its cadence is at least 20
  (as in the FIT file, so 40 spm for a run) and, if it has power, its
  power is at least 20 W. Cadence sensors don't just drop to zero when
  the rider stops pedaling: on Jan's May 11 ride only 48 of 9313 samples
  were zero. Coasting reads as cadence decaying to 1 to 5 rpm over
  several seconds, as a reading held for many seconds (40 rpm at 1 to 4
  W, slowing from 38 to 13 km/h), and, when pedaling again, as a lagging
  6 to 13 rpm while the power meter already reads 60 to 120 W. The power
  shows whether the rider pedals; activities without it rely on the
  cadence floor alone.
- Min Cadence is the lowest average over 30 consecutive records that are
  all pedaling: the slowest cadence held for about 30 seconds. On the May
  11 ride that's 59.5 rpm at 192 W, where the first version of this rule
  (the lowest 3 samples away from zeros) found 1 rpm while coasting. It
  marks the record that ends those 30. A short activity, or lap, may have
  no such stretch, and then has no Min Cadence.
- Avg Cadence is the average while pedaling (81 rpm on the May 11 ride,
  where leaving out only the zeros gives 75).

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
`max_at`. Min and average need the recorded cadence and power, so
`analyze()` collects them, before it smooths the records, and hands them
to a free function, in `storage/Activity.h`:

```cpp
struct CadenceSample {
    size_t             at;      // the record's index
    u8                 cadence; // as in the FIT file: rpm, or strides per minute
    std::optional<u16> power;   // the record's power, if it has one
};

struct CadenceStats {
    float32                average { 0 }; // while pedaling
    std::optional<float32> min { };       // over 30 records of pedaling
    size_t                 min_at { 0 };
};

std::optional<CadenceStats> cadence_stats(std::span<CadenceSample const> samples);
```

`PedalingCadence` (20), `PedalingPower` (20 W) and `MinCadenceRecords`
(30) are constants in `Activity.h`. `cadence_stats()` returns nothing when
no sample is pedaling. `min` is the lowest average of 30 samples of
consecutive records, all pedaling; a record without cadence breaks the
run. `min_at` is the last of those records. The result goes in a new
`Segment::cadence`.

`data_fields()` says "rpm" for rides, doubles the cadence for running,
walking and hiking and says "spm", and shows other sports' cadence as it
is, with "/min".

**Laps are analyzed before the activity's own segment.** The activity's
`analyze()` writes the smoothed values back into the records, and the laps
analyze the same records. Analyzing them first gives them the recorded
values, as the activity's segment gets.

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
- `cadence_stats()`: the average is over the samples that are pedaling;
  a sample below 20, or with power below 20 W, isn't; the minimum is over
  30 consecutive records, all pedaling, and a record without cadence
  breaks the run; no minimum without 30 such records; nothing when no
  sample is pedaling; `min_at`. Also the May 11 ride's coasting patterns:
  a decay to 1 rpm, and a held reading at low power.
- A lap's cadence is from the recorded values, not the smoothed ones.
- `data_fields()`: cadence in rpm for rides, doubled in spm for runs,
  walks and hikes, and as recorded in "/min" for other sports,
  Max and Min Cadence clickable with their records, no cadence lines below
  the realistic maximum; the first line is only Elapsed Time.
- TSS over the moving time.
- `activity_summary()`: all three parts; without distance; without TSS
  (no power, or too few samples for NP).

Loading `total_ascent` from a FIT file isn't tested: there's no FIT test
data in the repository. Checked by hand on a real activity, against the
climb Garmin Connect shows. Drawing the summary is checked by hand too.

## Review notes

1. **Bug: a lap's cadence is worked out from the smoothed cadence.**
   `Activity::analyze()` (`src/App/storage/Activity.cpp`, around line 219)
   calls `segment.analyze(records, true)` first, which overwrites each
   record's cadence with its 3-sample average (line 105), and only then
   `lap.segment.analyze(..., false)` on the same records (line 236). So
   `cadence_stats()` for a lap gets smoothed samples: a stop isn't a zero
   any more, its neighbors are pulled down, the average includes them, and
   the 5-record margin around stops has nothing to work from. With 20
   records of 90 rpm and a stop at record 5, the session has an average
   and minimum of 90, the lap 85 and 80. Nothing shows a lap's cadence
   yet, but `Lap::segment.cadence` is there, and the first lap display
   would show these numbers. (HR and power for laps are smoothed twice in
   the same way, which predates this branch.) The cadence samples could be
   collected before the session smooths the records, or the laps analyzed
   first. `test_lap_cadence_is_from_the_recorded_cadence` in
   `test/ActivityLoadTest.cpp` fails on this.
2. **Suggestion: doubling the cadence for every sport that isn't a ride.**
   `add_cadence_lines()` (`src/App/widget/DataFields.cpp`, around line
   160) doubles the cadence for any sport other than Cycling and E-biking,
   as the spec says. For running and walking that's right, but FIT records
   swimming, rowing and paddling cadence in strokes per minute, so a row at
   28 strokes/min shows as "56 spm". Worth deciding in a later spec whether
   to double only for foot sports, and what to call the others.
3. **Missing test, added: loading `total_ascent`.** It can be tested
   without FIT files in the repository: `test/ActivityLoadTest.cpp` builds
   a FIT file byte by byte and loads it with `load_from_slice()`. It checks
   that the session's `total_ascent` loads and is what `climb()` returns,
   that FIT's invalid value 0xFFFF leaves it empty, that the session's
   other fields still load, and that laps don't get it and work out their
   own climb with the hysteresis. These pass.
4. **Missing test, added: edges of `cadence_stats()` and the climb.** In
   `test/DataFieldsTest.cpp`: a sample exactly 5 records before or after a
   stop is near it, 6 isn't; a stop in the first 5 records; the margin is
   counted in records across a dropout, not in samples; one or two samples
   give an average but no minimum; and the climb's threshold (exactly 3 m
   counts, 2.9 m doesn't, and a descent lowers the reference). These pass.

### Resolution

1. Fixed: `Activity::analyze()` analyzes the laps first, and the activity's
   segment, which smooths the records, last. This also ends the double
   smoothing of lap HR and power. `test_lap_cadence_is_from_the_recorded_cadence`
   passes, and fails with the old order.
2. Fixed in this spec rather than a later one: the cadence is doubled for
   running, walking and hiking only. Rides stay in rpm, and other sports
   show their cadence as recorded, as "/min".
   `test_walk_and_hike_cadence_in_spm` and `test_rowing_cadence_as_recorded`
   cover it.
3. and 4. Kept. The tests in 4 for the 5-record margin went with the rule
   itself: checked by hand on the May 11 ride, it gave a Min Cadence of 1
   rpm, because coasting rarely reads as zero (see Cadence, above). The
   rule is now pedaling-based, with its own tests, including that ride's
   coasting patterns, each of which fails without the power check.
