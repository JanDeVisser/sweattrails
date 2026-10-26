# Edit an Activity's Name

Status: In progress

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

### Editing a line of text: `TextLine` in `src/Lib/Widget/TextLine.h`

- **`struct TextLine`**: the text as a `rune_string` (`wchar_t`, 32 bits
  on macOS and Linux, so a codepoint each) and the cursor as an index
  into it. That makes moving and deleting a character at a time plain
  indexing, and the text goes to UTF-8 only when it's read
  (`utf8()`) or set (`set(std::string_view)`). Plain logic, no raylib,
  for the tests.
  - `insert(rune)`, which leaves out control characters;
    `paste(std::string_view utf8)`, with line breaks as spaces;
    `backspace()`, `del()`, `left()`, `right()`, `home()`, `end()`.
  - `handle_key(modifier, key, clipboard)`: the keys, with Cmd+V
    (`KModSuper`) on macOS and Ctrl+V elsewhere pasting what
    `clipboard()` returns, which is `GetClipboardText()` in the box and
    made up in tests. Returns whether it took the key.
  - `scroll_for(cursor_x, width)`: the sideways scroll that keeps the
    cursor in view, given where the cursor is in the whole text, as the
    font measures it: kept in `scroll_x`, so that it only moves when
    the cursor would leave the box.
- **`input_box()`** keeps its interface, and its `InputBox` uses a
  `TextLine`: `character()` and `process_key()` pass on to it, `draw()`
  measures the text before the cursor, and the box's Enter hands
  `utf8()` to the submit function.
- A `static_assert` that `rune` is 32 bits: on Windows, where `wchar_t`
  is 16, a character wouldn't be one `rune`, and the README says
  Windows isn't supported.

### The sidecar: `storage/Activity.h`

- **`JSONValue::erase(key)`** in `JSON.h`, which there isn't yet, to
  remove `"title"`.
- **Reading**: `load_from_dir()` takes the service's name from the
  sidecar, as now, into `title`, keeps that as `original_title`, and then
  takes `"title"` if there is one. `original_title` is what an empty name
  goes back to. `sidecar_title()` keeps its meaning, the service's name,
  and a new `sidecar_own_title()` reads `"title"`.
- **Writing**: `save_sidecar_title(sidecar, title)` (free function, for
  the tests): reads the sidecar if there is one, refuses if it isn't a
  JSON object, sets or erases `"title"`, and writes it with
  `write_file_atomically()`. A sidecar left without keys, which only the
  app made, is removed rather than written as `{}`.
- **`Activity::rename(name)`**: trims the name, does nothing if it's the
  same, saves it to `<dir>/<id.name>.json`, and sets `title`, to the
  name or to `original_title`. Returns an error for the status line.

### Renaming in the app

- The task `st-rename-activity` (F2) in SweatTrails.cpp: not under a
  modal, nor without an activity on display, as the lap keys. It opens
  `input_box()` with the prompt "Name" and the title, and its submit
  function calls `rename()` on the displayed activity's `Activity`, and
  sets the status line if it fails.
- **The click on the title**: `ActivityDataDisplay` keeps where it drew
  the title, as it does for the clickable fields, asks for the pointing
  hand over it, and on a click submits `st-rename-activity` to the bus,
  so that the key and the click are one code path.

## Test plan

- `TextLineTest` (new): typing ASCII and "Café", "Rückweg", "Ålesund",
  and an emoji; the cursor, Backspace and Delete a character at a time;
  Home and End; control characters left out; pasting, with line breaks
  as spaces, at the cursor; Cmd+V and Ctrl+V; `utf8()` and `set()`
  round trips; `scroll_for()` keeping the cursor in view both ways.
- `JSONTest`: `erase()`, of a key that's there and one that isn't.
- `ActivityLoadTest`: `"title"` wins over `activityName` and `name`;
  `save_sidecar_title()` into a Garmin sidecar keeps its other keys,
  makes a sidecar without one, erases `"title"` for an empty name and
  removes a sidecar left empty, and refuses one that isn't JSON, leaving
  it as it was; `rename()` trims, and an empty name brings back the
  original title.
- By hand: F2 and clicking the title, typing and pasting non-ASCII, a
  long name scrolling in the box, Esc, the month's list after a rename,
  a rename that fails (a read-only directory), and F2 under the month's
  list.

## Review notes

*Filled in by the Reviewer.*
