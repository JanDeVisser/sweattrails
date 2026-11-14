# Small UI upgrades: double click, the status bar's hint, copying the file's path

Status: In progress, on branch feature/53-small-ui-upgrades

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

Three small things that make the app quicker to use with the mouse:

1. Double click an entry in a list to choose it, as Enter does.
2. Click the hint in the status bar to open the command palette.
3. Copy the activity's file name to the clipboard from the activity info
   popup.

## Feature

### What there is now

- **Lists** are `ListBox` modals (`src/Lib/Widget/Modal.h`): the
  activity list (`ActivityList`, `widget/ActivitySelector.*`), the
  command palette (`CommandList`, `src/Lib/Widget/CommandPalette.*`), and
  the questions with choices (`QueryBox`) and picks (`PickBox`). The
  mouse only scrolls them, with the wheel and the scrollbar: a click on
  an entry does nothing. The keys move the selection, and Enter submits
  it.
- **The status bar** (`StatusBar` in `src/Lib/Widget/Widget.h`, spec
  [47](47-status-bar.md)) shows, when idle, a summary of the store on
  the left and, dimmed on the right, "Cmd+Shift+P for commands". A
  message replaces both. Nothing in it reacts to the mouse.
- **The activity info popup** (`ActivityInfoBox`, spec
  [48](48-device-info.md)) shows the FIT file, under "File", relative to
  the store ("2026/10/1791310974.fit"), or its full path for one outside
  it. F, or a click on the hint line, shows the file in Finder. Nothing
  copies to the clipboard yet; raylib has `SetClipboardText()`.

### Double click in a list

- A **single click** on an entry selects it, as the arrow keys would; a
  **double click** on an entry submits it, as Enter does. Jan agreed: a
  single click selecting is what people expect, and the double click
  builds on it.
- It works the same in every `ListBox`, so it goes in `ListBox` itself,
  in `src/Lib/Widget`: the activity list, the command palette, and the
  questions and picks all get it.
- A double click is two clicks on the same entry within some 400 ms.
  raylib has no double click of its own, so the Developer picks the
  time.
- A click on the scrollbar or outside the entries does what it does now.

### The status bar's hint

- A click on the hint opens the command palette, as its key does. The
  mouse cursor is a pointing hand over it, as over the info popup's
  hint, so that it reads as clickable.
- Only the hint: the summary on the left and messages don't react.
  While a message replaces the hint, there's nothing to click.

### Copying the file's path

- **C** in the activity info popup, or a **click on the file's line**,
  copies the file's **full path** to the clipboard:
  `/Users/…/sweattrails/activity/2026/10/1791310974.fit`. The full path,
  not the name relative to the store that the popup shows, so that it
  pastes into a terminal or an Open dialog.
- The hint at the bottom of the popup says so: "F: Show in Finder · C:
  copy path · Tab: next recording · Esc closes". Without a file, as for
  an activity loaded otherwise, there's no C and nothing to click.
- Once copied, "Copied" shows in place of the hint for a moment (some 2
  seconds), and then the hint comes back.
- The pointing hand shows over the file's line too.

### Version 1 scope

In: all three as above.

Not planned: right-click menus, dragging entries, and copying other
lines of the popup.

## Open questions

## Architecture

### Double click (`src/Lib/Widget/Scroll.h`, `Modal.h`)

- `DoubleClick`, next to `ScrollInput` in `Scroll.h`, as it works on
  its `time`: `bool click(int target, double now)` says whether this
  click on `target` (a row) makes a double click with the one before, on
  the same target within `DoubleClick::Interval`, 0.4 s. A third click
  starts over. Plain data, so a test drives it with times; `Table` can
  use it too one day.
- `ListBox::process_input()` goes through `handle_mouse(ScrollInput)`,
  which a test can call: the scrollbar first, as now; else
  `entry_at(mouse)`, from `rows_area` and `scroll.lines`, as `draw()`
  left them, and `scroll.top_line`. A click selects that entry; a double
  click also submits it, through `choose()`, which Enter now uses too.
- `handle_scrolling()` becomes `handle_mouse()`; its callers
  (`ListBoxTest`) follow.

### The status bar's hint (`src/Lib/Widget/Widget.*`, `SweatTrails.cpp`)

- `StatusBar` gets `std::function<void()> on_hint` and `hint_area`,
  where `draw()` drew the hint, empty when it didn't draw it (a message,
  or no room). `handle_input(ScrollInput)`, from `process_input()`:
  over the hint, the pointing hand, and a click calls `on_hint`.
- The app sets `on_hint` to submit `st-command-palette`. Under a modal
  the app's widgets get no input, so the bar doesn't react then.

### Copying the path (`widget/ActivityInfoBox.*`, `SweatTrails.cpp`)

- The constructor gets a fourth function, `Copy`, as it gets `Reveal`:
  the app's is `SetClipboardText()`, which needs a window, and a test's
  records what it was given.
- `copy()` copies `info.path` and remembers when, `copied_at`. C calls
  it, and so does a click on the file's line, `file_area`, which
  `draw()` records as it records `hint_area`.
- `hint_text(double now)`: "Copied" within `CopiedSeconds`, 2 s, of
  `copied_at`, else `info_hint()`, which says "C: copy path" after F's.
  `draw()` draws `hint_text(GetTime())`. Switching recordings with Tab
  ends the "Copied".

## Test plan

- `ScrollTest` (or `ListBoxTest`): `DoubleClick` at and over the
  interval, on another target, and a third click.
- `ListBoxTest`: with `rows_area`, `scroll.lines` and `top_line` set by
  hand, a click selects the entry under it, scrolled too; a double click
  submits; a click below the last entry or on the scrollbar doesn't.
- `StatusBarTest`: a click in `hint_area` calls `on_hint`, one outside
  doesn't, nor one while a message shows.
- `ActivityInfoBoxTest`: C copies the full path, through the test's
  `Copy`; without a path nothing; `hint_text()` before, within and after
  2 s; `info_hint()` with "C: copy path"; a click on `file_area`.
- Not automated: the drawing, and the pointing hand.

## Review notes

*Filled in by the Reviewer.*
