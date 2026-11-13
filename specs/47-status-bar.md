# Status bar

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

The status line at the bottom of the window is always there, so the
window doesn't jump by a line each time a message comes or goes. When
nothing is going on, it shows a short summary of the store and a hint to
the command palette, so that a new user finds the palette.

## Feature

### What there is now

- `SweatTrails::set_status(text)` (`SweatTrails.cpp`) puts a message in
  a `Label` at the bottom of the window, on DARKGRAY. While the text is
  empty the label's policy is `SizePolicy::Hide`, so it takes no room;
  the first message shows it, and `resize()` lays the window out again,
  moving everything above it up by a line. Clearing it moves everything
  back.
- Messages are progress ("Reading activities for the totals: 3 of 40"),
  results ("Grouped 2 activities recorded more than once") and errors
  ("Couldn't save the name: …"). `set_status("")` clears it, which jobs
  do when they end; while a login waits, an empty status shows "Waiting
  for the Strava login in your browser" instead.
- The command palette opens with Cmd+Shift+P (Ctrl+Shift+P off macOS).

### Always shown

- **The bar is always one line high**, at the bottom of the window, also
  when it has nothing to say. The layout no longer changes with it.
- **A message is shown as now**, and `set_status("")` goes back to the
  idle text rather than hiding the bar.

### When idle

- **On the left, a summary of the store**: how many activities there
  are, as many as the activity list shows, and the date of the newest,
  as the activity list writes dates: "1,234 activities, the newest on
  Tue 12 Nov 2026". An empty store says "No activities yet".
- **It is kept up to date**: after a download, an import, grouping, or
  anything else that adds or removes activities.
- **On the right, a hint to the palette**, dimmed so that it doesn't
  read as a message: "Cmd+Shift+P for commands", or "Ctrl+Shift+P for
  commands" off macOS, the key the app binds.
- **A message replaces both.** The hint isn't shown under a message, so
  a long one has the whole bar.

### Version 1 scope

In: the bar always shown, the summary of the store, and the hint.

Not planned: keeping errors until they are dismissed, a history of
messages, and when the last download ran, which isn't recorded anywhere
yet. A message is cleared as now, by the next one or by
`set_status("")`.

## Open questions

None.

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
