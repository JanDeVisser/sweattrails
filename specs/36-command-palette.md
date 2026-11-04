# Command palette

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

The user finds and runs any of the app's actions by typing part of its
name, as in VS Code's and Zed's command palette, without remembering its
key. It also shows each action's key, so the palette teaches the keys.

## Feature

### What there is now

- Every action is a task on the bus, `bus.add_task()` in
  `SweatTrails.cpp`: 44 of them, such as `st-garmin-download` and
  `st-summaries-curve`. Specs 07, 08, 09, 10 and 26 made them tasks for
  this palette.
- A task has an id, an owner, a handler, its key bindings, and
  optionally a `when()` predicate for when its keys work, and
  `run_under_modals()`. It has **no human-readable title**: the ids are
  for code. Spec 07 deferred the titles to this spec.
- Some tasks are bound several times with different arguments, such as
  `st-summaries-period` to W, M and Y with `"week"`, `"month"` and
  `"year"`; others take the key itself as their argument.
- `ListBox` (`src/Lib/Widget/Modal.h`) is a modal list with type-to-
  search (case-insensitive substring), columns at the row's end, and
  sorting: the activity list uses it.
- `modifier_string()` names modifiers; there's nothing yet that names a
  `KeyCombo` for display, such as "Ctrl+Shift+G" or "⌘⇧R".

### Opening it

- **Cmd+Shift+P** on macOS, **Ctrl+Shift+P** elsewhere, as in VS Code
  and Zed, and as rename's Cmd+Shift+R is Ctrl+Shift+R off macOS. The
  palette is a task itself, `st-command-palette`, with a title, as every
  action is.
- It's a modal, as the activity list is: it doesn't open while another
  modal is open, and Escape closes it without running anything.

### What it lists

- **Tasks that have a title.** A task gets its title where it's added,
  next to its key: "Download from Garmin Connect", "Rename activity".
  Every action the user can take gets one; tasks that are plumbing,
  such as `st-message`, or that only move through a list, such as the
  summaries' Up and Down, get none, and aren't listed.
- **An entry per binding** for a task bound with different arguments:
  `st-summaries-period` is "Totals by week", "Totals by month" and
  "Totals by year", each with its own key. Such a binding carries its
  own title. A task whose bindings differ only by key, such as Enter and
  the keypad's Enter, is one entry.
- **Only what works now**: a task whose `when()` is false, such as the
  summaries' keys while the activity screen is showing, isn't listed, so
  nothing listed does nothing. A task without keys, such as logging in
  to Wahoo, is listed too: the palette is the only way to it without a
  key.
- Each entry shows its key at the row's end, if it has one, in words
  as the README writes keys: "Ctrl+G", "Cmd+Shift+R". Not macOS's "⌘⇧R":
  the app's font may not have those glyphs, which would draw as boxes. A
  task with several keys shows the first.
- **The titles** are short, and start with a verb where there is one:
  "Download from Garmin Connect", "Rename activity", "Show the totals",
  "Totals by week". The Developer writes them for all the tasks, and Jan
  checks them in the review.

### Searching and running

- **Typing filters by words in any order**: each word typed must appear
  in the title, case-insensitively, as part of a word or a whole one.
  "garmin down" finds "Download from Garmin Connect". This is the list
  box's search with words; fuzzy matching, as VS Code's "dlgc", is left
  out *(deferred)*: it needs ranking, and its matches surprise.
- **The order**: the commands last run from the palette first, the most
  recent at the top, then the rest from A to Z. The recent ones are
  remembered while the app runs, not saved *(deferred)*. While typing,
  the matches keep that order.
- **Enter runs the selected command**, as its key would have, on the
  screen the palette was opened over, with the binding's arguments, and
  closes the palette. A command that asks for input, such as renaming,
  asks as it does from its key.
- Commands that would take arguments typed into the palette, such as
  the download's `days`, run with their defaults, as from their key:
  typing arguments is left out *(deferred)*.

### README

The README says how to open the palette, near the top, with the keys,
as the way to find any command.

### Version 1 scope

Everything above. Deferred:

- Fuzzy matching *(deferred)*.
- Remembering the recent commands across restarts *(deferred)*.
- Arguments typed into the palette *(deferred)*.
- Changing a command's key from the palette *(deferred)*.

## Open questions

None.

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
