# Extending the selection to the next km, lap or stop

Status: In progress

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

Dragging in the graph selects a range of an activity
(`specs/25-range-stats.md`), but only as precisely as the mouse. To get
the stats of "from the 5 km mark to the end of lap 3", or "from the
traffic light to the next stop", the user has to drag pixel by pixel.
This spec lets a right-click on the graph extend the selection backwards
or forwards to the next whole km, lap boundary or stop. It was split off
from `specs/37-graph-improvements.md`, and builds on its graph.

## Feature

### What there is now

- A drag in the graph selects a range of records,
  `DisplayedActivity::segment`. When the drag ends, `end_segment()`
  clears a range under `MinRangeSeconds`, 10 s, and otherwise works out
  its stats for the data panel with `range_segment()`.
- A selected lap is a segment too, with `DisplayedActivity::lap` set.
  A drag ends a lap's selection.
- Ctrl+L makes the dragged range a lap (`specs/26-after-the-fact-laps.md`).
- Stops aren't recorded as such: the app reads no FIT `event` messages.
  `range_segment()`'s moving time works them out from the records: a
  gap of more than `RangeMovingGap`, 10 s, between two records is a
  pause, and a record slower than `RangeMovingSpeed`, 0.5 m/s, stands
  still.
- Nothing in the app handles a right-click yet, and there's no pop-up
  menu widget. The command palette (`specs/36-command-palette.md`) is a
  `ListBox` of `Command`s in a modal, which a menu could build on. The
  menu belongs in `src/Lib/Widget`.

### The menu

A right-click in the graph's plot opens a pop-up menu at the mouse, with
six items:

| Item | Moves |
|---|---|
| Back to the km | the start, to the whole km before it |
| Back to the lap | the start, to the start of a lap |
| Back to the stop | the start, to the end of the stop before it |
| On to the km | the end, to the whole km after it |
| On to the lap | the end, to the end of a lap |
| On to the stop | the end, to the start of the stop after it |

- Each item moves one end of the selection, and leaves the other where
  it is. Choosing an item again steps on to the next km, lap or stop:
  a boundary the end is already on doesn't count.
- The menu is the same wherever in the plot the click is. A right-click
  in the lap strip, the legend or the labels does nothing, as the left
  button there doesn't start a drag either.
- Up and Down move through the items, Enter chooses one, and a click
  chooses the item under the mouse. Esc, or a click outside the menu,
  closes it without changing anything.
- The menu stays on screen: near the window's right or bottom edge, it
  opens to the left of or above the mouse.
- **An item with nothing to go to is greyed out**, can't be chosen, and
  is skipped by Up and Down. That's the case when there's no km, lap
  boundary or stop left in that direction, or none at all: "Back to the
  km" and "On to the km" for an activity whose records have no
  distance, and both lap items for an activity with one lap.

### What the items select from

- **A selection**, dragged or extended before, is where they start
  from.
- **A selected lap** is a selection too. Extending it turns it into a
  range, as dragging does: the lap is no longer selected, and the data
  panel shows the range's stats under its "12.4–18.9 km" heading. So
  "On to the lap" on lap 3 selects laps 3 and 4.
- **Without a selection**, the record right-clicked is where it starts:
  "On to the km" selects from it to the next whole km, and "Back to the
  km" from the whole km before it up to it. A second right-click extends
  that further.

### The boundaries

- **A km** is the first record at or past each whole km of the records'
  distance, as the graph's km labels are placed (spec 37). "On to the
  km" ends the selection at the first record at or past the next whole
  km after the selection's end. "Back to the km" starts it at the first
  record at or past the last whole km before its start, or at the first
  record for the first km.
- **A lap** boundary is where one of the activity's laps starts and
  ends: the device's laps, or those made afterwards (spec 26). "On to
  the lap" ends the selection at the last record of the lap its end is
  in, or of the next lap if the end is already the last record of a lap.
  "Back to the lap" mirrors it, with the first record of a lap.
- **A stop** is where the activity stood still: a pause of more than
  `RangeMovingGap`, 10 s, between two records, which recorded nothing,
  or 10 s or more of records slower than `RangeMovingSpeed`, 0.5 m/s.
  These are the thresholds of the moving time, so a stop is what the
  moving time leaves out, except that a slowdown of under 10 s, a sharp
  turn say, isn't one. "On to the stop" ends the selection at the last
  record before the next stop, the last one still moving. "Back to the
  stop" starts it at the first record after the stop before it, the
  first one moving again. Records without a speed, from an indoor ride
  say, are never standing still, so only pauses are stops there.

### After an extension

- The data panel shows the new range's stats and heading, as when a
  drag ends (spec 25), and the map and graph show the range.
- A range made by extending isn't cleared for being under
  `MinRangeSeconds`: it was chosen, not dragged by accident. A
  right-click a few metres before a km and "On to the km" is a range of
  a second or two, which the next "On to the km" makes a km longer.
- Ctrl+L makes it a lap, as it does a dragged range.
- While comparing recordings (spec 33), it works the same, on the
  recording on display.

### Commands

- Each of the six items is a task with a title, registered with
  `bus.add_task()` and `.titled()`, so it's in the command palette on
  the activity screen as "Selection: back to the km" and so on. The menu
  runs the same tasks.
- They have no keys by default. Six key combinations is a lot to learn
  for something that's rarely needed; keys can come later.
- From the palette, there's no right-click point. Without a selection,
  the mark, the record last hovered, takes its place. Without a mark
  either, a message says "Drag or right-click in the graph to select a
  range first", as Ctrl+L does. An item with nothing to go to does
  nothing from the palette, where it's listed anyway, as a download
  running is.

### Version 1 scope

In: the right-click menu with its six items, greyed out where they
can't go, from a selection, a lap or the clicked record; stops worked
out from the records; the six commands in the palette.

Deferred:

- Keys for the six commands *(deferred)*.
- Stops from the FIT file's timer events *(deferred)*: the records'
  stops work for every source, Zwift and Strava included.
- Miles *(deferred)*.
- Shrinking the selection back to a km, lap or stop *(deferred)*.
- Zooming the graph to the selection, and out again:
  `specs/39-graph-zoom-and-pace.md` *(deferred)*. It changes how the graph maps x to records, for the
  lines, labels, strip, mark and dragging alike, which is a spec of its
  own. It adds "Zoom to selection" and "Zoom out" to this menu.
- Other right-click menus, such as on the map or the lap table
  *(deferred)*, though the menu widget should be general enough for
  them.

## Open questions

None.

## Architecture

### Decisions

1. **The boundaries go in a new `src/App/storage/Boundaries.h/.cpp`.**
   They're activity logic, with no widget in it, so a test reaches them
   without a window.

   ```cpp
   enum class Boundary { Km, Lap, Stop };
   enum class Direction { Back, On };

   // Where the activity stood still: the last record moving before it,
   // and the first moving again after it.
   struct Stop {
       size_t last_moving;
       size_t first_moving;
   };
   // From the records as recorded: a pause of more than RangeMovingGap,
   // or RangeMovingGap seconds or more below RangeMovingSpeed.
   std::vector<Stop> stops(std::span<record const> recorded);

   // The record the selection's start (Back) or end (On) moves to from
   // record `from`, or none if there's nothing to go to.
   std::optional<size_t> extend_to(Activity const &activity, size_t from, Boundary boundary, Direction direction);
   ```

2. **`DisplayedActivity` gets `extend(Boundary, Direction)`** and
   `can_extend(Boundary, Direction)`. They start from the selection,
   dragged or a lap's, or else from the mark. A new range clears the
   lap and gets its stats straight away, without `end_segment()`'s 10 s
   rule. `extend()` returns false when there's nothing to start from,
   for the palette's message.
3. **The right-click point is the mark.** Hovering the plot already sets
   the mark to the record under the mouse, and the menu is a modal, so
   the graph gets no input while it's open and the mark stays put. The
   palette's fallback is the mark too, so this needs no new state.
4. **A new `PopupMenu` in `src/Lib/Widget/PopupMenu.h/.cpp`.** It's a
   `Modal` with items `{ title, enabled, action }`, opened at a point.
   It is not a `ListBox`: that one is a centred dialog with a prompt and
   a search, and has no greyed-out items or clicks to choose. The menu
   handles:
   - Up and Down, skipping greyed-out items;
   - Enter, and Esc;
   - the item under the mouse, which a click chooses;
   - a click outside, which closes it;
   - staying on screen, worked out by a pure `menu_position(point,
     size, screen)`.

   On a choice, the menu closes and runs the item's action. Its header
   joins `.clang-format`'s Widget regex.
5. **Six tasks, one per item**, such as `st-extend-on-km`, each
   `.titled("Selection: on to the km")` and `.when(on_activity_screen)`,
   with no keys. They share one handler with the boundary and direction
   bound in. The menu's actions submit them to the bus, as the palette
   does, so that both run the same code.
6. **`ActivityGraph::process_input()` opens the menu** on a right-click
   in the plot, through a function in `SweatTrails.cpp` that builds the
   items from `can_extend()`. The graph reaches it with
   `SweatTrails::the()`, as the map does for its jobs.

## Test plan

- A new `boundaries_test` (`test/BoundariesTest.cpp`):
  - `stops()`:
    - a pause of more than 10 s;
    - 10 s or more of standing still;
    - a slowdown under 10 s, which isn't a stop;
    - records without speed;
    - a stop at the start or end of the activity.
  - `extend_to()`, both ways, for each of km, lap and stop:
    - from inside a stretch;
    - from a record already on a boundary, which steps to the next;
    - none left in that direction;
    - "Back to the km" in the first km, which goes to the first record;
    - no distance, and one lap.
- `DisplayedActivityTest`:
  - extending a dragged range, a selected lap, and from the mark;
  - nothing to start from;
  - the lap is cleared, and the stats are there;
  - a short range isn't cleared.
- A new `popup_menu_test` (`test/PopupMenuTest.cpp`), with a
  `ScopedTestApp`:
  - Up and Down skip greyed-out items;
  - Enter runs the selected item's action and Esc runs none;
  - a menu with every item greyed out;
  - `menu_position()` near each edge of the screen.
- By hand in the app:
  - drawing the menu;
  - the right-click;
  - choosing an item with the mouse;
  - a click outside it.

## Review notes

*Filled in by the Reviewer.*
