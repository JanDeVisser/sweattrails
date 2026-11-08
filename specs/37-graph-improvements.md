# Graph improvements

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

The activity screen's graph shows the shape of an activity, but not its
numbers: there are no axes, and nothing says which line is which or what
its scale is. Its power and speed lines are 1-second noise, in dark
colours that are hard to read. This spec makes the graph readable: time
and distance along it, a legend with each line's range, and smoothed
power and speed in lighter colours. It also fixes two flaws: the graph
doesn't follow the window's size, and a dropout draws a straight line
where there's no data.

Extending the selection with a right-click, to the next km, lap or stop,
is spec 38, which builds on this one.

## Feature

### The graph as it is

The graph under the data panel and the map (`ActivityGraph` in
`src/App/widget/Activity.cpp`) is 200 pixels high and as wide as the
window less 20 pixels. It's drawn once into an image when the activity is
set:

- Altitude is a light gray area, over the activity's elevation range.
- Speed is a dark green line from 0 to the highest speed, also for runs.
- Heart rate is a red line over its range, from lowest to highest.
- Power is a dark blue line from 0 to the highest power.
- Cadence isn't drawn, except when comparing recordings (spec 33), which
  draws power, heart rate and cadence of both recordings instead.

The x axis is the record's index, not time or distance, so a pause that
the device recorded as a gap without records takes no room. There are
no axes, ticks or labels, and no legend. Every record is drawn
unsmoothed, so 1-second power and GPS speed are noisy. Outside comparing,
a dropout in heart rate or power isn't a gap: the line jumps from the
last value to the next one. `resize()` does nothing, so the graph keeps
its width when the window changes size until another activity is shown.

Hovering shows a mark with a banner of the record's altitude, power,
speed and heart rate. Dragging selects a range (spec 25). The lap strip
under the graph selects a lap (spec 09).

### Layout

From top to bottom, the graph widget has:

1. **The legend**, one row in the small font, at the top left.
2. **The km labels**, one row in the small font, for activities with a
   distance.
3. **The plot**, as high as now.
4. **The lap strip**, as now.
5. **The time labels**, one row in the small font.

The widget grows by one small-font row, so that the plot keeps its
height. The data panel and the map get that much less room. The top band
that holds the hover banner now has the legend and the km labels in its
place.

The x axis stays by record, as now, so that the lap strip, dragging, the
selected range and the mark all work as they do. The labels follow the
records.

### Time and distance labels

- **Time** runs along the bottom, under the lap strip. It's the
  elapsed time since the activity's first record, as in "0:10" or
  "1:30", in hours and minutes. Each label goes at the first record at
  or past its time, with a short tick mark down from the plot. A pause
  that recorded nothing bunches the labels on either side of it, which
  shows where it was.
- **Distance** runs along the top of the plot, under the legend: "5 km",
  at the first record at or past it, with a short tick mark up from the
  plot. An activity without distances, such as an indoor ride without a
  speed sensor, has no km row, and the band is left empty.
- The steps are chosen so that labels are at least about 80 pixels
  apart, from 1, 2, 5, 10, 15 and 30 minutes and 1 and 2 hours for time,
  and 1, 2, 5, 10, 20, 50 and 100 km for distance. `nice_span()` in
  `Chart.h` already does this for the summaries' charts.
- The labels are in kilometres, as the rest of the app is.

### Legend

The legend lists the lines drawn, each in its line's colour, as
"Alt 12–86 m", "Spd 0–48 km/h", "HR 102–178 bpm" or "Pwr 0–612 W". The
range is the one the line is drawn over: from 0 to the highest value for
power and speed, and from the lowest to the highest for heart rate and
altitude. For power and speed that's the highest smoothed value, so
that the line fills the plot, and it can be below the data panel's
maximum.

While the mouse is over the graph, the legend shows the values at the
mark instead: "Alt 45 m  Spd 32.4 km/h  HR 145 bpm  Pwr 245 W". The
floating banner next to the mark goes away, while the mark's vertical
line stays. A value the record doesn't have is left out, as the banner
does now. The values are the record's own, not smoothed, as the data
panel's are.

A faint horizontal line across the middle of the plot helps to read the
lines against their ranges.

While comparing recordings (spec 33), the legend lists power, heart
rate and cadence, with both recordings' ranges taken together, as the
lines are drawn. While hovering, it shows the values of both, as the
banner does now, from `compare_banner()`: "245 / 238 W".

### Smoothing

- Each pixel column draws the average of the records in it, where it now
  draws one of them and skips the rest.
- Power and speed are smoothed with a 5-second rolling average before
  that. Heart rate, cadence and altitude are drawn as they are.
- The rolling average doesn't reach across a pause of more than 10
  seconds without records, `RangeMovingGap`, nor across a dropout.
- The same smoothing applies while comparing, to both recordings' power.
- There's no setting or key for it.

### Fixes

- **Resizing.** The graph is drawn again when its widget changes size,
  such as when the window is resized or maximized, and not only when
  another activity is shown.
- **Dropouts.** A record without heart rate, power or speed breaks that
  line: it's a gap, as it is while comparing, and doesn't join the
  values on either side with a straight line. Altitude's area is left as
  it is.

### Colours

Dark blue power and dark green speed are hard to read on the dark
background, so they get lighter:

- **Power** becomes raylib's `SKYBLUE`, (102, 191, 255), in place of
  `DARKBLUE`. `PowerColor` in `DataFields.h` changes, so this applies
  wherever power is shown: the graph's line, the legend, the data panel
  while comparing, and the compared lines, where the other recording's
  stays a lighter shade of it.
- **Speed** becomes raylib's `LIME`, (0, 158, 47), in place of
  `DARKGREEN`. It gets a `SpeedColor` constant next to `PowerColor`,
  `HeartRateColor` and `CadenceColor`, used for its line, the legend and
  the hover values, where `DARKGREEN` is now written out.

### Version 1 scope

In: the layout, time and km labels, the legend with ranges and hover
values, smoothing, the lighter power and speed colours, and the two
fixes.

Deferred:

- Extending the selection with a right-click, to the next km, lap or
  stop: spec 38 *(deferred)*.
- Pace instead of speed for runs, turning lines on and off, and cadence
  outside comparing *(deferred)*.
- Zooming into a range *(deferred)*.
- Miles *(deferred)*.

## Open questions

None.

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
