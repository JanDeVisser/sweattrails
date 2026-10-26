# Edit an Activity's Name

Status: Ready

## Goal

The user can give an activity a name of their own, such as "Ride to
Niagara with Anna" instead of "Morning Ride", or a name for an activity
that only has its file name. The name shows in the data panel and in the
month's list, and stays after the app restarts.

## How it works now

- `Activity::title` comes from, from lowest to highest priority:
  - the FIT file's name, `Activity::id.name`, such as `garmin-1234`;
  - the FIT file's workout name (`wkt_name`), if it has one;
  - the name in the download's sidecar, `<name>.json` next to the FIT
    file: Garmin Connect's `activityName`, or Wahoo's `name`
    (`sidecar_title()`). Older sidecars, from the Rungap webhook, have no
    name, and activities copied in by hand have no sidecar.
- Nothing is cached: the month's list (`ActivitySelector`) loads each
  activity, and its sidecar, every time it opens. So a name saved where
  `Activity::load_from_dir()` reads it shows there the next time the
  list opens.
- The data panel shows the title in the large font, under the sport and
  start time.
- `src/Lib/Widget/Modal.h` has `input_box()`, a modal with a prompt and a
  line of text. It isn't used anywhere yet, and has gaps:
  - `character()` casts the codepoint raylib reports to a `char`, so
    anything outside ASCII, such as "é" or "ü", comes out wrong;
  - the cursor is a byte index, and is drawn as if every character were
    a cell wide;
  - there's no Delete, Home or End, and no pasting.
- The downloads write the sidecar with `JSONValue::serialize()`, whose
  objects are `std::map`s, so the keys are sorted, and whose doubles
  have 6 decimals (issue #3). A later download of the same activity
  doesn't replace its sidecar: duplicates go to the duplicates
  directory.

## Feature

### Renaming

- **F2**, the task `st-rename-activity`, or **clicking the title** in
  the data panel opens a box, "Name", with the activity's name in it, the
  cursor at its end. Over the title, the mouse cursor is a pointing
  hand, as over the maxima that mark a record. F2 rather than Ctrl+R,
  which often means reload.
- **Enter** saves the name; **Esc** closes the box and leaves the name as
  it was. A name that's the same as before isn't saved.
- The data panel shows the new name right away. The month's list shows
  it the next time it opens, since it loads the activities every time.
- Like the other keys, F2 does nothing under a modal, and nothing
  without an activity on display.
- **A box rather than editing the title in place** in the data panel: it
  needs no text field in the panel's layout, for a feature that's used
  now and then. Editing in place can come with a text field widget
  *(deferred)*.

### The box: fixing `input_box()`

`input_box()` in `src/Lib/Widget/Modal.h` is the box, and it gets what a
name needs, for every use of it after this one too:

- **Text outside ASCII**, as UTF-8: "Café ride", "Rückweg", "Ålesund".
  The cursor moves, and Backspace and Delete delete, a character at a
  time, not a byte, and it's drawn after the text before it as the font
  measures it, not at a fixed width per character.
- **Delete**, **Home** and **End**, besides Left, Right and Backspace.
- **Pasting** with Cmd+V on macOS and Ctrl+V elsewhere, at the cursor.
  Line breaks in what's pasted become spaces: a name is one line.
- **A text longer than the box** scrolls sideways, so that the cursor
  stays in view.
- Control characters, such as a tab, aren't typed in.
- Selecting text, with the mouse or Shift and the arrows, is deferred
  *(deferred)*: a long name is cleared with Home and Delete, or by
  holding Backspace.

### Where the name is saved

- **In the activity's sidecar, `<name>.json`, under a key of
  SweatTrails' own, `"title"`.** It wins over the other names when the
  activity is loaded: `"title"`, then Garmin Connect's `activityName`,
  then Wahoo's `name`, then the FIT file's workout name, then the file
  name.
- **The service's name stays as it came.** `activityName` or `name` is
  the name the activity had when it was downloaded, so there's no need
  to keep it under another key: removing `"title"` brings it back.
- **The rest of the sidecar is kept**: it's read, `"title"` is set, and
  it's written back whole, atomically, so that the app quitting halfway
  doesn't leave half a file. Rewriting it loses nothing: the download
  wrote it with the same code, so its numbers already have the 6
  decimals of issue #3, and its keys are already sorted.
- **Without a sidecar**, such as for a FIT file copied in by hand,
  renaming makes one, with only `"title"`.
- **A sidecar that isn't JSON** isn't overwritten: the status line says
  the name couldn't be saved, and why.
- Saving an empty name, or only spaces, removes `"title"`, and the
  activity has its original name again. Spaces around a name are
  trimmed.

### Version 1 scope

Everything above. Deferred:

- Renaming the activity in Garmin Connect, Wahoo or Strava too: the app
  only reads from them, and each would need write access and its own
  API calls *(deferred)*.
- Editing the title in place in the data panel *(deferred)*.
- Selecting text in the box *(deferred)*.
- Notes, or other edits, in the sidecar: `"title"` is the first key of
  SweatTrails' own there, and others can follow *(deferred)*.

## Open questions

None.

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
