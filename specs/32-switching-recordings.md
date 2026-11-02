# Switching between the recordings of an activity

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

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
