# Editing the description

Status: In review

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

Write and change an activity's description in the app: how it went, what
the weather was, who came along. Today the description comes only from
Strava, and can't be changed here. Split off from
[40](40-activity-display-overhaul.md).

## Feature

### What there is now

- The data panel shows `Activity::notes` under the title, in the small
  font, wrapped, `NotesLines` = 3 lines at most, cut short with "…".
  Nothing edits them.
- The notes come from the sidecar: Strava's description, under
  `"strava"` (`specs/24-strava-titles.md`), or else the old app's
  `.strava` file's. `Activity::read_names()` picks them.
- The user's own title is `"title"` in the sidecar, saved by
  `Activity::rename()` through `save_sidecar_title()`, and wins over
  Strava's.
- `TextLine` in `src/Lib/Widget` edits a single line, with no drawing, so
  that tests can type into it. `input_box()` in `Modal.h` draws one in a
  modal, which the rename uses.
- A pen after the title renames (spec 40).

### Where the description is kept

- **The user's own description is `"notes"` in the sidecar**, a key of
  SweatTrails' own next to `"title"`, and wins over Strava's and the
  `.strava` file's, as the user's title wins over theirs. The rest of the
  sidecar is kept, through `update_sidecar()`.
- **A Strava sync never changes it**: the sync only writes `"strava"`.
  A description Strava fetches later is kept there, under the user's.
- **Saving it empty removes `"notes"`**, which brings Strava's back, as
  an empty name brings back the title from before the rename. So there's
  no way to have no description where Strava has one; that seems fine.
- **Nothing is sent to Strava**: that would need Strava's write scope, a
  new login, and its own failures and limits *(deferred)*.
- An activity without a sidecar gets one, as with a rename. A sidecar
  that isn't JSON isn't overwritten, and saving says so on the status
  line, as the rename does.

### The editor

- **A modal, like the rename box**, over the screen: about half the
  screen wide and ten lines high, with "Description" at the top and the
  description in it, wrapped at word boundaries to the box's width, with
  the cursor at its end. A description longer than ten lines scrolls, to
  keep the cursor in view, and with the mouse wheel.
- **Enter starts a new line. Cmd+Enter (Ctrl+Enter off macOS) saves.**
  Saving strips white space from the start and the end, as a rename
  does, and the panel shows the new description right away.
- **Esc cancels.** If the text differs from what it was when the editor
  opened, it asks first, "Discard the changes?" with Yes and No, with
  `query_box()`: Yes closes without saving, No goes back to editing with
  the text as it was. Unchanged, Esc just closes. A paragraph is more
  work to lose than a name.
- **Keys:**
  - typing, Backspace and Delete, a character at a time;
  - Left and Right a character, and Up and Down a line, as wrapped;
  - Home and End to the start and end of the line, as wrapped;
    Cmd+Up and Cmd+Down (Ctrl+Home and Ctrl+End off macOS) to the start
    and end of the text;
  - Alt+Left and Alt+Right (Ctrl off macOS) a word at a time, and
    Alt+Backspace (Ctrl off macOS) deleting the word before the cursor;
  - Cmd+V (Ctrl+V off macOS) pastes, keeping line breaks. A tab is
    typed as a space, as other control characters aren't typed at all,
    as in `TextLine`.
- **Clicking in the text moves the cursor there.**
- Not in version 1: selecting text, copy and cut *(deferred)*, and undo
  *(deferred)*: Esc gets back what was saved.
- The data panel's layout stays as it is while editing, and shows the
  saved description afterwards, as the panel's height is worked out
  again (spec 40).
- **The editing part is a widget of the library**, `src/Lib/Widget`, as
  `TextLine` is: a multiline text with its cursor, wrapping and keys,
  testable without a window, and something to use for other long texts.

### Opening it

- **A pen after the description**, as after the title: grey, white with
  the pointing hand while the mouse is over it. It goes after the last
  line of the notes as shown, which may end in "…".
- **With no description, a grey "Add a description" line** where the
  notes would go, in the small font, with the pen after it. Clicking the
  line or the pen opens the editor, empty. The panel's height counts
  that line as a line of notes.
- **"Edit description" in the command palette**: a task,
  `st-edit-description`, on the activity screen. No key of its own, and
  clicking the description itself doesn't open it.

### Version 1 scope

In: the user's own description in the sidecar, the editor and its
widget, the pen and the placeholder, and the palette command.

Deferred: sending the description to Strava; selecting, copying and
cutting text; undo. Emoji are spec [42](42-emoji.md).

## Open questions

None.

## Architecture

### What's there

- `Activity::read_names()` picks the title and the notes from the
  sidecar (`sidecar_titles()`), the `.strava` file and the FIT file.
  `Activity::rename()` trims, keeps `original_title` for an empty name,
  does nothing for an unchanged one, and saves with
  `save_sidecar_title()`, through `update_sidecar()`.
- `TextLine` (`src/Lib/Widget`) is the model of a line being edited:
  runes, a cursor, and the keys, with no drawing. `input_box()` in
  `Modal.h` is the modal that draws one. `query_box()` asks Yes or No.
- `App` sends a modal its characters through `character()` and its keys,
  repeats too, through `process_key()`.
- `wrap_words()` in `StringUtil` wraps text to strings, which lose where
  the cursor is: the editor needs its lines as positions in the text.
- `ActivityDataDisplay::rows()` places the notes, which spec 40 made the
  one place both drawing and the panel's height go through.

### Decisions

1. **Storage.** `SidecarTitles` gets `own_notes`, the sidecar's
   `"notes"`, and `save_sidecar_notes(sidecar, notes)` saves or removes
   it, as `save_sidecar_title()` does the title. `read_names()` takes
   `own_notes` over Strava's and the `.strava` file's, and keeps those in
   a new `Activity::original_notes`, as `original_title` keeps the title.
   `Activity::set_notes(text)` works as `rename()` does: it trims, goes
   back to `original_notes` when empty, does nothing when unchanged (so
   opening the editor on Strava's description and saving it doesn't copy
   it into `"notes"`), and saves.
2. **`TextArea`**, a new model in `src/Lib/Widget/TextArea.h`, next to
   `TextLine`, and like it with no drawing:
   - the text as runes with `\n` for line breaks, and the cursor;
   - `wrap(width, measure)` gives the lines as wrapped, each a range of
     the text, breaking after a space, and inside a word that's wider
     than the line by itself. `measure` is a function, so that tests
     pass one that counts characters, and the modal one that asks the
     font. The model keeps the lines, wrapped again after each change;
   - the keys of the spec, with the platform's modifiers as `TextLine`
     has them (Alt or Ctrl for words, Cmd+Up and Down or Ctrl+Home and
     End for the text's ends). Up and Down remember the x they started
     from, so that a short line in between doesn't pull the cursor left;
   - `cursor_at(line, x)` for a click, and `scroll_to_cursor()` and
     `scroll(lines)` for the box's `top_line` and its visible lines.
3. **The modal**, `text_area_box(target, prompt, submit, text)`, in a
   new `src/Lib/Widget/TextAreaBox.h`, as `input_box()` is in `Modal.h`:
   draws the `TextArea`, half the screen wide and ten lines high, takes
   characters, keys, clicks and the wheel, saves with Cmd+Enter or
   Ctrl+Enter, and on Esc with the text changed asks "Discard the
   changes?" with `query_box()`. It's the library's, so a generic
   editor of long texts. Both headers go in `.clang-format`'s Widget
   list.
4. **The task** `st-edit-description`, "Edit description", on the
   activity screen, opens the modal on the displayed activity's notes;
   saving calls `set_notes()`, says what went wrong on the status line as
   the rename does, and resizes the activity display, as the notes can
   change the panel's height.
5. **The panel.** `rows()` gives the notes' lines, or "Add a
   description" as one line when there are none, and draws that in
   `GRAY`. The pen, the same texture as the title's, goes after the last
   line, and a click on it, or on the placeholder, submits
   `st-edit-description`. The hover and drawing are the title's pen's,
   made a function both use.

## Test plan

- `activity_load_test` (or where `rename` is tested):
  - `save_sidecar_notes()` saves and removes `"notes"`, keeping the rest
    of the sidecar;
  - `read_names()`: the user's notes over Strava's, and Strava's back
    once they're removed;
  - `set_notes()` trims, saves, does nothing unchanged (no `"notes"` for
    Strava's own text), and goes back to Strava's when empty.
- `text_area_test`, new, with a measure of 10 px a character:
  - typing, Enter, Backspace and Delete, also across a line break;
  - wrapping: at spaces, a word wider than the line broken, line
    breaks, an empty last line after a break;
  - Left and Right, Up and Down across wrapped lines and breaks, with
    the x remembered over a short line; Home and End of a wrapped line;
    the text's start and end;
  - word moves and Alt+Backspace;
  - pasting `\r\n`, `\r` and tabs;
  - `cursor_at()` for clicks past a line's end and below the last line;
  - scrolling to the cursor, and with the wheel, within the text.
- By hand: the pen and the placeholder, the editor's look, keys and
  mouse, Esc's question, saving and the panel afterwards, a Strava sync
  leaving the user's notes alone.

## Review notes

*Filled in by the Reviewer.*
