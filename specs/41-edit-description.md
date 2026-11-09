# Editing the description

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

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
