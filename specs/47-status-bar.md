# Status bar

Status: Done

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

### `StatusBar` in `src/Lib/Widget`

The `Label` at the bottom of the window becomes a `StatusBar`, a widget
in `Widget.h`/`Widget.cpp` next to `Label`, as nothing in it is about
activities:

```cpp
// One line at the bottom of a window, always there. A message has the
// whole line; without one, `idle` is on the left and `hint`, dimmed, on
// the right.
struct StatusBar : public Widget {
    std::string message { };
    std::string idle { };
    std::string hint { };

    struct Shown {
        std::string_view left { };
        std::string_view right { }; // Dimmed
    };
    // What draw() draws: a test can ask without a window.
    [[nodiscard]] Shown shown() const;
    void draw() override;
};
```

Its policy is `SizePolicy::Characters`, 1.0, always, so `set_status()`
no longer calls `resize()`. `draw()` renders `left` at `PADDING` in
RAYWHITE and `right` right-aligned (`render_text()` with a negative x) in
LIGHTGRAY, both on DARKGRAY. (GRAY is too faint to read on DARKGRAY.)

### The summary of the store

```cpp
// src/App/storage/Summaries.h
// The status bar's summary of the store: "1,234 activities, the newest on
// Thu 12 Nov 2026", or "No activities yet".
[[nodiscard]] std::string store_summary(ActivitySummaries const &summaries);
```

It counts from the summaries the `UpdateSummaries` job keeps
(`SweatTrails::summaries`), rather than listing the store again:

- They are of the FIT files in the months' directories, so main
  recordings only, as in the activity list. A summary without a sport is
  of a file that can't be read, which the list leaves out too, and so
  does the count.
- They are updated after everything that adds or removes activities: the
  inbox (downloads, imports, dropped files, and grouping new
  recordings), the once over the store for recordings, and a FIT file
  that changes. So `notif_summaries_updated()` sets `status->idle`, and
  nothing else has to.
- The count uses `integer_to_string(n, 10, ',')` for "1,234", and "1
  activity" for one. Its grouping was wrong ("12,34" for 1234), and
  nothing used it: it's fixed, with a test in `string_util_test`. The date is the newest start in local time, as
  `DateTime::from_timestamp()` gives it, written with `weekday_name()` and
  `month_name()` cut to three letters, as the activity list cuts them.

Until the first summaries come in, `idle` is empty, so the bar shows only
the hint rather than "No activities yet" for a moment at startup.

### `SweatTrails`

- `status` is a `std::shared_ptr<StatusBar>`; `hint` is set once from
  `key_text(palette_key)` plus " for commands", so it is the key the app
  binds.
- `set_status(text)` sets `status->message`, with the login's waiting
  text for an empty one as now, and no longer resizes.

## Test plan

- A new `status_bar_test` (`test/StatusBarTest.cpp`),
  with a `ScopedTestApp`: `shown()` gives the message alone on the left
  when there is one, and `idle` and `hint` when there isn't; an empty
  `idle` gives an empty left.
- `summaries_test`: `store_summary()` for no summaries, for only
  unreadable ones ("No activities yet"), for one ("1 activity, …"), for
  1,234, with an unreadable one newer than the newest readable one, and
  for summaries out of order. Starts at noon UTC, so that the local date
  is the same in any time zone the tests run in.
- By hand: the window doesn't move when a download starts and ends; the
  summary changes after a download; the hint shows the key.

## Review notes

1. Bug: the hint is drawn in the wrong place. `StatusBar::draw()`
   (`src/Lib/Widget/Widget.cpp`, the `render_text(-(width + PADDING), ...)`
   call) passes a negative x, but `render_sized_text_()` already subtracts
   the text's width for a negative x (`x = viewport.width - total.x + x`),
   so the text's right edge ends up at `-x` from the right edge. With
   `-(width + PADDING)` the hint ends a whole hint-width plus PADDING
   short of the right edge (about 25 characters in), and is not
   right-aligned. It should be `render_text(-PADDING, ...)`, as the
   palette's `render_text(-8, 8, search, ...)` in `Modal.h`. No window-free
   test is possible for this; check it by hand.
2. Suggestion: with a narrow window, a long idle text on the left and the
   hint on the right overlap; nothing clips or drops the hint. Rare, and
   the spec doesn't ask for it.
3. Suggestion: in `test/SummariesTest.cpp` the comment "A file with longer
   curves than there are durations ..." now sits above the new
   `Nov12Noon` and `numbered()` helpers instead of above
   `test_curves_no_longer_than_their_lists()`. Move it back.

How they were resolved, with Jan:

1. Fixed (8ae9a88): the hint is drawn at `-PADDING`.
2. Fixed (f4bff49): `StatusBar::hint_fits()` says whether the hint fits
   next to the text on the left, with PADDING at the ends and twice that
   between them; `draw()` leaves the hint out if it doesn't. Tested in
   `status_bar_test`.
3. Fixed (6c551e2): the comment is back above its test.

Everything else in Version 1 scope is there: the bar is always one line,
`set_status()` no longer resizes, the idle text comes from
`notif_summaries_updated()`, unreadable summaries are skipped, and the
grouping fix is right and tested. Build and the full test run pass (69 of
69). No tests added: the Test plan is covered, and the bug in 1 needs a
window.
