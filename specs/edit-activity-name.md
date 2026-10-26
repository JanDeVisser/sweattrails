# Edit an Activity's Name

Status: In review

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

- **F2** or **Cmd+Shift+R** (Ctrl+Shift+R off macOS), the task
  `st-rename-activity`, or **clicking the title** in
  the data panel opens a box, "Name", with the activity's name in it, the
  cursor at its end. Over the title, the mouse cursor is a pointing
  hand, as over the maxima that mark a record. F2 rather than Ctrl+R,
  which often means reload. Cmd+Shift+R is for keyboards whose function
  keys don't reach the app, as Jan found when testing by hand.
- **Enter** saves the name; **Esc** closes the box and leaves the name as
  it was. A name that's the same as before isn't saved.
- The data panel shows the new name right away. The month's list shows
  it the next time it opens, since it loads the activities every time.
- Like the other keys, F2 and Cmd+Shift+R do nothing under a modal, and nothing
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
    `insert()` also leaves out runes that aren't Unicode characters;
    `paste(std::string_view utf8)`, with line breaks as spaces;
    `backspace()`, `del()`, `left()`, `right()`, `home()`, `end()`.
  - `handle_key(modifier, key, clipboard)`: the keys, with Cmd+V
    (`KModSuper`) on macOS and Ctrl+V elsewhere pasting what
    `clipboard()` returns, which is `GetClipboardText()` in the box and
    made up in tests. Returns whether it took the key.
  - `scroll_for(cursor_x, text_width, width)`: the sideways scroll that
    keeps the cursor in view, given where the cursor is in the whole
    text, and how wide that is, as the font measures them: kept in
    `scroll_x`, so that it only moves when the cursor would leave the
    box, or when the text gets shorter and there's room on the right.
- **`input_box()`** keeps its interface, and its `InputBox` uses a
  `TextLine`: `character()` and `process_key()` pass on to it, `draw()`
  measures the text before the cursor, and the box's Enter hands
  `utf8()` to the submit function. It had never been instantiated, and
  didn't compile once it was: it asserted that its submit function, a
  lambda, wasn't null, and its `dismiss()` called itself rather than the
  dismiss function. Both are fixed.
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
  and a new `sidecar_own_title()` reads `"title"`. `load_from_dir()`
  takes both from `sidecar_titles()`, which reads the sidecar once.
- **Writing**: `save_sidecar_title(sidecar, title)` (free function, for
  the tests): reads the sidecar if there is one, refuses if it isn't a
  JSON object, sets or erases `"title"`, and writes it with
  `write_file_atomically()`. A sidecar left without keys, which only the
  app made, is removed rather than written as `{}`.
- **`Activity::rename(name)`**: trims the name, does nothing if it's the
  same, saves it to the sidecar, and sets `title`, to the name or to
  `original_title`. Returns an error for the status line.
  `load_from_dir()` keeps the sidecar's path in `Activity::sidecar`, so
  that an activity loaded from another directory, as in the tests, saves
  where it was loaded from; one loaded otherwise, such as from the inbox,
  has none, and can't be renamed.

### Renaming in the app

- The task `st-rename-activity` (F2, Cmd+Shift+R) in SweatTrails.cpp: not under a
  modal, nor without an activity on display, as the lap keys, with the
  helper they share, now `activity_for_keys()`. It opens
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

1. **Bug: `input_box()` doesn't compile with a lambda as dismiss
   function** (`src/Lib/Widget/Modal.h:380`). `InputBox`'s constructor
   defaults `Dismiss dismiss = nullptr`. `InputBox` is a local class, so
   its default arguments are instantiated together with `input_box()`,
   and a lambda can't be made from `nullptr`:
   `input_box(target, "Name", submit, "", [](auto const &) {})` fails with
   "no viable conversion from 'std::nullptr_t'". The Architecture says the
   `dismiss()` fix is done, but it was never compiled with a dismiss
   function, and a lambda is how every other caller passes functions.
   Fix: drop the default from the constructor, since `input_box()` always
   passes `dismiss`, or default to `Dismiss { }`. `InputBoxTest` uses a
   function pointer for now; switch it to a lambda once this is fixed.
2. **Bug: a long name looks cleared while most of it is still there**
   (`src/Lib/Widget/TextLine.cpp:142`, `scroll_for()`). The scroll only
   moves when the cursor leaves the box, and never goes back as the text
   gets shorter. Take a name wider than the box, with the cursor at its
   end, and hold Backspace, which the spec gives as the way to clear a long
   name. The right part of the box empties first. Once the cursor reaches
   the left edge, `scroll_x` follows the cursor, so the cursor stays at
   the left edge with nothing after it, and the box looks empty while
   everything before the cursor is still in the name. Enter then saves a
   name the user can't see. With numbers: `scroll_for(900, 400)` gives
   500, then `scroll_for(300, 400)` gives 300, and the 300 px of text
   before the cursor are out of view. Fix: also pass the width of the
   whole text, and clamp
   `scroll_x` to `max(0, text_width - width)`, so that the end of the text
   stays at the right edge while there is text to show on the left.
3. **Suggestion: F2 opens the box for an activity that can't be renamed**
   (`src/App/SweatTrails.cpp:495`, `src/App/storage/Activity.cpp:286`).
   An activity without a `sidecar` path, such as one loaded from a file
   (the inbox), opens the box, and only after the user has typed a name
   does Enter say "can't be renamed". Check `activity->activity.sidecar`
   in `cmd_rename_activity()`, and set the status line instead of opening
   the box. Don't show the pointing hand over the title for such an
   activity either.
4. **Suggestion: a rune that isn't a valid codepoint brings the app down
   in `draw_text()`** (`src/Lib/Widget/Modal.h:486`,
   `src/Lib/Widget/TextLine.cpp:39`). `insert()` takes any non-control
   `rune`, including surrogates (0xD800 to 0xDFFF) and values above
   0x10FFFF. `as_utf8()` is `MUST_EVAL(to_utf8())`, so the next frame
   aborts, and `utf8()` (line 34) turns the whole text into "", which on
   Enter removes the user's name without a word. GLFW doesn't report
   such codepoints on macOS or Linux, so it's defensive. Fix: have
   `insert()` refuse them, and use `utf8()` rather than
   `as_utf8()` in `draw_text()`.
5. **Suggestion: `strip()` passes negative `char`s to `std::isspace`**
   (`src/Lib/Base/StringUtil.cpp:156` and `:162`). `rename()` now sends
   it non-ASCII names. "Café" ends in byte 0xA9, which is a negative
   `char`, and that's undefined behavior. It's harmless today because the
   app runs in the "C" locale. Casting to `unsigned char` alone isn't
   enough: under a UTF-8 locale on macOS, `isspace(0xA0)` is true, and
   that would chop the last byte off "Voilà" (C3 A0). Fix: trim ASCII
   whitespace only.
6. **Suggestion: `load_from_dir()` reads and parses the sidecar twice**
   (`src/App/storage/Activity.cpp:175-179`), once for the service's name
   and once for `"title"`, for every activity each time the month's list
   opens. Read it once with `read_sidecar()` and take both names from
   the same `JSONValue`.

Tests added by the Reviewer: `test/InputBoxTest.cpp`
(`input_box_test`: Enter and keypad Enter submit the UTF-8 text, Esc
dismisses with and without a dismiss function, the editing keys, F2 and
keys with unknown modifiers go on to the app). In `ActivityLoadTest`: a
rename to the same name makes no sidecar, no rename without a sidecar
path, no `.part` file left behind, a rename in a read-only directory, and
typing the service's name over the user's. In `TextLineTest`: editing
next to a character outside the BMP, control characters and a lone `\r`
in a paste, an empty clipboard, and `set()` resetting the scroll. They
all pass. The full run (`ctest -E oauth`) passes, 22 of 22.

### Resolutions

1. Fixed (`8ad08e8`): the constructor has no default, and `InputBoxTest`
   uses a lambda.
2. Fixed (`f4b0e8a`): `scroll_for()` takes the width of the text and
   clamps to it; `test_scroll_back_as_the_text_gets_shorter`.
3. Not changed, at Jan's call: renaming an activity without a sidecar
   should make one, and it does. Every activity on display is loaded with
   `load_from_dir()`, which always sets the sidecar's path, whether the
   file exists or not; only the inbox's import, which never displays, has
   none. `test_rename_back_to_the_workouts_name` now checks that the
   rename makes the sidecar and that the name is there after a reload
   (`72b28d4`).
4. Fixed (`1d99e54`): `insert()` refuses surrogates and runes outside
   0 to 0x10FFFF, and `draw_text()` converts with `to_utf8()`, without
   aborting; `test_runes_that_arent_characters_are_not_typed`.
5. Fixed (`23b0058`): `strip()`, `lstrip()` and `rstrip()` trim ASCII
   whitespace only, with `find_first_not_of()`; this also fixes
   `rstrip()` reading before an empty or all-blank string. `split()`
   still uses `isspace()`, but no name goes through it. `test_strip` in
   `StringUtilTest`.
6. Fixed (`0b66017`): `sidecar_titles()` reads the sidecar once, for
   both names; the two functions that read one name use it.
