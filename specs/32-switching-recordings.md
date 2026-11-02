# Switching between the recordings of an activity

Status: In progress, on branch feature/32-switching-recordings

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

An activity recorded on several devices is kept as one, shown as its
main recording (`31-multiple-recordings.md`). The user records twice to
compare devices and power meters, so they want to look at the other
recordings too: its map, data, laps and graph, as that device recorded
them. They also want to make another recording the main one, from what
they see.

This is the second of the three specs in `31-multiple-recordings.md`.
Comparing the recordings side by side is `33-comparing-recordings.md`.

## Feature

### What there is now

- The main recording is listed and shown. The others are FIT files in
  `<year>/<month>/<start>.recordings/`, next to it, each with its own
  sidecar: they aren't listed, and nothing can show them.
- The summary line under the title says "· 2 recordings".
- Ctrl+J, on the activity on display, groups its recordings with it as
  the main one, and keeps it so (`"recordings": "main"` in its sidecar).
  It only works on a listed activity, that is, a main one.
- Ctrl+Shift+R (Ctrl+Alt+R off macOS) splits the group.
- An activity knows its device's maker (`manufacturer`). The FIT file
  also has a product number, and sometimes a product name; there's no
  table of product numbers.

### Switching

- **Tab** shows the activity's next recording, **Shift+Tab** the one
  before, wrapping around. With two recordings, the usual case, Tab
  flips between them, which makes comparing them easy. It works on the
  activity screen only (as the lap keys do), and on an activity with one
  recording it does nothing: a stray Tab shouldn't bring up a message.
- The order is the **main one first**, then the others by their start
  time, so recording 1 is always the main one.
- What a recording shows is everything the activity screen shows, as
  that device recorded it: the map, the data panel, the laps and the
  graph.
- **What a flip keeps**: the map's place and zoom, and the selected lap,
  by its number, if the other recording has that lap. A range dragged in
  the graph is dropped, as the recordings' times differ; comparing a
  stretch is for `33-comparing-recordings.md`.

### Which recording is shown

- The summary line says which, where it now says "· 2 recordings":
  "42.3 km · 1:25:10 · TSS 87 · Recording 2 of 2: Wahoo". The main one
  says "Recording 1 of 2: Garmin". Its distance, time and TSS are the
  recording's.
- A **device** is named by FIT's maker, "Garmin", "Wahoo", with the
  product name after it when the FIT file has one: "Wahoo ELEMNT
  ROAM". Garmin's product numbers, which would give "Garmin Edge 840",
  need a table of them *(deferred)*.
- The **title** is the activity's, the main one's, on every recording.

### Keys on a recording that isn't the main one

- **Ctrl+J** asks "Make this the main recording?", and on Yes makes it
  the main one, chosen by hand as Ctrl+J's is (`31-multiple-recordings.md`):
  it's listed, where the inbox would file it, the main one before goes
  into its folder with the others, and the activity's title moves to it
  if it has none of its own. It's then shown as recording 1. This is
  spec 31's "choosing another by hand".
- **F2** renames the activity: the main one's title, which every
  recording shows. A recording's own title, from Strava say, isn't shown
  while it's in the group.
- **Laps** (Ctrl+L, Ctrl+Shift+L, Ctrl+Alt+L), **filling a gap** by
  dropping a file, **Ctrl+Shift+F**, and the **overlay** (Ctrl+O,
  Ctrl+Shift+O, Ctrl+E) work on the recording shown: its FIT file and
  its sidecar, as its laps and fills are in its own times.
- **Ctrl+Shift+R** splits the whole group, as on the main one.
  Afterwards the recording shown is an activity of its own, and stays on
  display.
- **Left, Right and Ctrl+M** work as from the activity: Left and Right
  go to the activity before or after, showing its main one, and Ctrl+M
  opens the month at the activity. Coming back to it shows the main one:
  which recording was shown isn't remembered.
- If grouping in the background (a download's inbox) changes the group
  of the recording shown, the activity is shown again, as its main one.

### README

The README's section on activities recorded on several devices says
that Tab and Shift+Tab switch between the recordings, what the summary
line says, that Ctrl+J on a recording makes it the main one, and that
the other keys work on the recording shown.

### Version 1 scope

Everything above. Deferred:

- Garmin's product names from their numbers *(deferred)*.
- Remembering which recording was shown *(deferred)*.
- A list of the recordings, with their devices and numbers side by
  side: comparing them is `33-comparing-recordings.md`.

## Open questions

None.

## Architecture

### A recording's identity (`storage/Types.h`, `storage/Activity.*`)

- **`ActivityID::recording`**: the stem of a recording in its main one's
  folder, `<name>.recordings/<recording>.fit`, and empty for the main one.
  `month` and `name` stay the main one's, so everything that goes by the
  activity works as from the activity without changes: Left and Right
  (`activity_next_to()` finds the activity by `name`), the last activity
  shown, and `show_main_again()` after grouping. `ActivityID::main()` is
  the ID without the recording, for Ctrl+M's list to select.
- **Loading**: `Activity::load()` reads a recording's FIT file from the
  folder. `sidecar` is the recording's own, so laps (`set_lap_starts()`)
  and fills (`fit_path()`, `original_path()`) work on the recording with
  no changes, and `show_again(id)` loads the recording again after them.
- **`Activity::activity_sidecar`**: the main one's sidecar, the same as
  `sidecar` for the main one. `read_names()` and `rename()` use it, so
  the title, notes and F2 are the activity's on every recording.
- **`Activity::recording_number`**, 1 for the main one, next to
  `recordings`, the count. Both come from the order below, on a deep
  load.
- **`Activity::product_name`**, from the FIT file's `file_id`, next to
  `manufacturer`.

### The order, and Ctrl+J and Ctrl+Shift+R (`storage/Recordings.*`)

- **`recordings_in_order(storage, id)`**: the IDs of the activity's
  recordings, the main one first, then the others by start time (their
  names). Tab and Shift+Tab step through it, and the load numbers them
  by it.
- **`main_of(fit)`**: the main one's FIT file of a recording in a folder;
  a listed one's is its own.
- **`make_main(storage, recording_fit)`**: Ctrl+J on a recording. The
  group is its main one and the folder's recordings. The marks are cleared,
  this one is marked `"main"`, and `group_recordings()` makes it the main
  one: it moves it to where the inbox would file it, and moves the former
  main one, with the rest of the folder, into the new one's folder. The
  title moves along if it has none. Only the group is grouped, and no
  other overlapping activities, as Ctrl+J on the main one does.
- **Ctrl+Shift+R on a recording** splits `main_of()` its file. The job
  then finds the recording by its start (`activity_starts()`) where it's
  filed now, and shows it, as the spec says.

### The screen (`widget/Activity.*`, `widget/DataFields.*`, `SweatTrails.cpp`)

- **`ActivityDisplay::switch_recording(activity)`**: `set_activity()`,
  keeping the map's view and the selected lap. `MapNavigator::keep(view,
  moved)` puts back the view from before, clamped to the new route, after
  `start()`. The lap is selected again by its number if the new
  recording has one; a dragged range isn't kept.
- **`SweatTrails::show_recording()`**: `show_activity()` with
  `switch_recording()`.
- **`st-next-recording` (Tab) and `st-previous-recording` (Shift+Tab)**,
  `.when(on_activity_screen)`: the next in `recordings_in_order()`,
  wrapping around, loaded with `load_to_show()`. They do nothing for one
  recording.
- **`device_name(activity)`** in `widget/DataFields.*`: the maker's name
  from a short table of the ones in use ("Garmin", "Wahoo", "Zwift",
  "Hammerhead", "Stages", "SRM", "Coros", "Polar", "Suunto"), else FIT's
  name with spaces for `_`. The product name follows it if there is one.
  `activity_summary()` says "· Recording 2 of 2: Wahoo ELEMNT ROAM"
  instead of "· 2 recordings".
- **Ctrl+J**, `cmd_group_recordings()`: on a recording it asks "Make
  this the main recording?" and runs the GroupWithMain job, which calls
  `make_main()` for a FIT file in a folder. It shows the new main one,
  by `activity_id_of(grouped.main)`.

## Test plan

- `RecordingsTest`, with a temp store:
  - `recordings_in_order()` for three recordings, and for one;
  - `main_of()`;
  - loading a recording's ID: its own records and sidecar, the main
    one's title, its number and the count;
  - laps saved on a recording go to its own sidecar, and F2's rename to
    the main one's;
  - `make_main()`: the recording listed and marked, the former main one
    and the rest in its folder, the title moving, a preferred recording
    coming in after it joining without taking its place;
  - splitting from a recording.
- `DataFieldsTest`: `device_name()` for a known maker, an unknown one,
  with and without a product name; the summary line on the main one and
  on a recording.
- `MapTest`: `MapNavigator::keep()` keeps a view on the new route, and
  clamps one off it.
- `ActivityDisplayTest` or `DisplayedActivityTest` with `ScopedTestApp`:
  `switch_recording()` keeps lap 3 when the other recording has it, and
  drops it when it hasn't.
- By hand: Tab and Shift+Tab on a compare ride, the summary line, the
  map staying put, laps and a fill on the other recording, Ctrl+J on it,
  and Ctrl+Shift+R from it.

## Review notes

*Filled in by the Reviewer.*
