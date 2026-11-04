# Command palette

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

### Where the spec leaves a gap

- **A task with titled bindings and untitled ones**: the titled ones
  are its entries, and the untitled ones, such as the keypad's Enter
  next to Enter, are other keys for one of them, not entries of their
  own. A task with a title and no titled binding is one entry, with its
  first binding's arguments and key; without a binding, with no
  arguments and no key.
- **The palette doesn't list itself.**
- **"The last few"** recent commands: up to 5, by title.
- **Words in any order for every list box**, not only the palette: the
  activity list's search (Ctrl+M) gets it too. Everything a substring
  matched before still matches, as each of its words is in the title.
- **Key names**: Cmd, Ctrl, Option and Shift on macOS (Super, Ctrl, Alt
  and Shift elsewhere), in that order, as the README writes them
  ("Cmd+Shift+R", "Ctrl+Alt+L"), then the key: "A", "F2", "Enter",
  "Esc", "Left", "Page Up", "Keypad +".

### The bus (`src/Lib/Widget/Widget.h`)

- `Task` gets an optional **`title`**, set with **`.titled("…")`**, as
  `.when()` and `.run_under_modals()` are chained.
- `Binding` gets an optional **`title`**, and `Task::bind()` an optional
  third argument for it: `.bind(KeyCombo { KEY_W, KModNone },
  summaries_argument("period", "week"), "Totals by week")`.
- **`Command { title, task, arguments, key }`**, and
  **`AppBus::commands()`**: the entries, by the rules above, of the
  tasks whose `when()` is true now, unsorted. Pure over the bus's tasks
  and bindings, so a test can build a bus and check them, as
  `KeyBindingTest` does.

### Keys and searching (`src/Lib/Widget`)

- **`key_text(KeyCombo)`** in `Widget.h`/`.cpp`, next to
  `modifier_string()`: "Cmd+Shift+P". Pure.
- **`matches_words(text, search)`** in `Modal.h`: each word of the
  search in the text, case-insensitively. `ListBox::filter()` uses it
  instead of `strcasestr()`.

### The palette (`src/Lib/Widget/CommandPalette.*`)

- It's general, not the app's: any app with tasks can have one.
- **`order_commands(commands, recent)`**, pure: the commands whose
  titles are in `recent`, most recent first, then the rest by title,
  A to Z, case-insensitively.
- **`RecentCommands`**: up to 5 titles, `used(title)` moving one to the
  front. The app keeps one, for as long as it runs.
- **`CommandPalette`**, a `ListBox<Command, { .search = true, .sort =
  false }>`, with the key in the row's column, and a function to run on
  Enter. **`command_palette(commands, recent, run)`** opens one.

### The app (`SweatTrails.cpp`)

- Every task gets `.titled()`, and the bindings with arguments their
  titles. Task `st-command-palette`, Cmd+Shift+P (Ctrl+Shift+P off
  macOS), not while a modal is open, which lists `bus.commands()` less
  itself, and on Enter marks the command used and submits its task with
  its arguments, which runs it next frame, with the palette closed.
- The README.

## Test plan

- `KeyBindingTest`: `commands()`: a titled task is an entry, an untitled
  one isn't; titled bindings are an entry each, with their arguments and
  keys, and an untitled binding of the same task isn't; a task without
  keys is an entry without a key; a task whose `when()` is false isn't.
- `KeyTextTest` (new, or in `KeyBindingTest`): `key_text()` for letters,
  F-keys, Enter, arrows, the keypad, and modifiers in order.
- `ListBoxTest`: `matches_words()`, and a list box's search by words in
  any order, which still matches what a substring did.
- `CommandPaletteTest` (new): `order_commands()` with and without
  recent ones, `RecentCommands` keeping 5 and moving a used one to the
  front, and the palette's Enter running the selected command and
  closing, with a search typed.
- By hand: Cmd+Shift+P on the activity screen and the summaries screen,
  and running a command that asks for input, such as renaming.

## Review notes

1. **Bug: "Back to the activity" doesn't always go back to the
   activity.** `st-summaries-close` (`SweatTrails.cpp:2052`) is Esc, and
   `cmd_summaries_close()` (`SweatTrails.cpp:1071`) first backs up a level
   and only goes to the activity at the top. On the totals, drilled into a
   month (Enter on a row), running "Back to the activity" from the palette
   shows the year again, as "Totals: back up" does, and leaves the user on
   the totals. The title should say what Esc does, such as "Totals: back
   up, or to the activity", or "Show or close the totals" (Ctrl+T) should
   be the palette's way back.
2. **Bug, from before this branch: a list box's search can't be typed in
   anything but ASCII.** `ListBox::character()` (`Modal.h:363`) keeps the
   low byte of raylib's codepoint, so typing "é" (U+00E9) searches for the
   byte 0xE9, which no UTF-8 title has. Typing "Té" finds nothing in the
   Ctrl+M list of a month with "Col du Télégraphe"; the same goes for the
   palette. Test `test_search_by_codepoint()` in `test/ListBoxTest.cpp`
   fails, and isn't run from `main()`.
3. **Suggestion: some listed commands do nothing now.** The spec says
   "nothing listed does nothing", but only `when()` takes commands out.
   While a download runs, "Download new activities", "Download from
   Garmin Connect", "Download from Wahoo" and "Get all the titles from
   Strava" are listed and return silently (`SweatTrails.cpp:664`, `690`);
   with an empty store, every activity-screen command is listed and does
   nothing (`activity_for_keys()` is null). A `when()` would also take
   their keys away, so it's Jan's call whether this matters.
4. **Suggestion: titles to look at.** "Totals: back" and "Totals: on"
   (`SweatTrails.cpp:2089`, `2090`) are hard to read without the README,
   which says Left and Right go "a year back or on" (a month in a month's
   weeks, 90 days in the fitness): such as "Totals: earlier" and "Totals:
   later". "Totals: year to date", "Totals: fitness" and "Totals: best
   power or pace" switch a view on and off but read as if they only show
   it, and have no verb; "Show or hide ..." would match "Show or close the
   totals". "Totals by week" and "Totals: running" are two forms for the
   same kind of choice. "Group the recordings, or make this one the main
   one" is long for a row that also shows a key. There are no duplicate
   titles, and every task the user can run has one; only `st-message`,
   `st-fill-gaps` (a dropped file) and `st-summaries-row` (Up and Down)
   don't, as the spec says.
5. **Suggestion: case-insensitive only for ASCII.** `matches_words()`
   (`Modal.h:110`) lowers byte by byte in the C locale (the app never calls
   `setlocale()`), so UTF-8 is never broken, but "TÉLÉ" doesn't find
   "Télégraphe". `strcasestr()` did the same, so this is no regression.
   `test_matches_words_utf8()` checks what it does now.
6. **Suggestion: `key_text()`'s fallback.** A key not in its table is
   "Key 336" (`Widget.cpp:160`), such as the keypad's = or Caps Lock. None
   is bound now, but a new binding on one would show that in the palette.
7. **Suggestion: `AppBus::bind(task, combo, arguments)`**
   (`Widget.h:470`) can't give a binding a title, while `Task::bind()`
   can.

Checked and fine: the rules of `commands()` (titled and untitled tasks
and bindings, `when()`, tasks without keys); every handler with the
arguments it gets from the palette, also the null ones of the keyless
downloads and logins (`make_download_request()` and `cmd_strava_login()`
check `is_object()`); handlers with `modals.empty()` checks run, as the
palette is popped before the bus runs the task, and `PendingTask` puts
the focus from before the palette back; `order_commands()`; the
palette's entries by position while searching.

Tests added: `KeyBindingTest` (`test_commands_of_titled_bindings`, an
untitled task with titled bindings and `when()`, as W, M and Y;
`test_commands_first_key`; `test_key_text_more`, the arrows, the keypad
and all four modifiers in order); `ListBoxTest`
(`test_matches_words_utf8`, and the failing `test_search_by_codepoint`,
not in `main()`); `ActivitySelectorTest` (`test_search_by_words`, Ctrl+M's
search by words, still of the title only); `CommandPaletteTest`
(`test_runs_the_selected_match`, `test_empty_palette`,
`test_task_runs_after_the_palette_closes`). The full run, `ctest -E
oauth`, passes.
