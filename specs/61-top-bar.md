# Top bar

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

Everything in the app is on keys or in the command palette, so there's
nothing on screen to click to get started or to find your way around. A
bar at the top of the window, on every screen, gives a menu to grow into,
and on the activity screen shows where the activity is in the store,
with the year and month clickable to browse from there.

It isn't a classic menu bar, with a row of menus: just one menu button
and, next to it, a breadcrumb trail.

## Feature

### What there is now

- Two screens in a `WidgetStack`: the activity screen
  (`ActivityDisplay`) and the totals (`SummariesScreen`). The status bar
  (`StatusBar`, `specs/47-status-bar.md`) is one line at the bottom of
  the window, under both.
- The activity's title is drawn at the top of the activity column, with
  its notes under it.
- Ctrl+Y opens the year list, Ctrl+M the list of the month's activities
  (`specs/60-year-month-navigation.md`).
- Quit is `st-quit`, Ctrl+Q, which asks whether you're sure.
- There are two kinds of menu: `ListBox` (`Modal.h`), a box in the middle
  of the window with a prompt, used for the activity list, the year and
  month lists, the overlay layouts and downloads; and `PopupMenu`
  (`PopupMenu.h`), a column of items that opens at a point, used for the
  graph's right-click menu.

### The bar

- **One line high, at the top of the window**, above the screen, on
  every screen, always there, like the status bar at the bottom.
- **A hamburger button (☰) at the top left.** Clicking it opens the menu.
- **The menu is a `PopupMenu`**, opened just below the button, as the
  graph's right-click menu opens at the mouse. Not a `ListBox`: that's a
  box in the middle of the window with a prompt, suited to a list to
  search through, not to a menu dropping from a button.
- **The menu has one item for now: Quit**, which runs `st-quit`, so it
  asks as Ctrl+Q does.

### The breadcrumb trail

On the activity screen, next to the hamburger:

```
☰  Activities › 2025 › November
```

Every crumb is clickable, and opens the list of what's in it, as a
breadcrumb goes into the level clicked:

- **Activities** opens the year list, as Ctrl+Y: the years with
  activities, with the year on display selected.
- **The year** opens the month list of that year, with the month on
  display selected, as Enter on the year in the year list does. Cmd+Up
  (Ctrl+Up off macOS) goes up to the year list from there.
- **The month** opens the list of that month's activities, as Ctrl+M,
  with the activity on display selected.

The trail stops at the month: the activity's title is at the top of the
activity column already.

With no activity on display, the trail is just "Activities", which
opens the year list as Ctrl+Y does then: with the latest year selected,
or a message when there are no activities. The clicks do nothing while
a modal is open, as the keys don't.

### The totals screen

Only the hamburger, no trail. The screen has its own large heading
already, so a "Totals" label would repeat it. *(Deferred)*: a trail for
the totals' drill-down, such as "Totals › 2025 › March", which would be
a spec of its own, as it hangs on how drilling down works.

### README

The README mentions the bar: the menu, and that the trail's crumbs open
the year, month and activity lists.

### Version 1 scope

In: the bar on every screen, the hamburger and its menu with Quit, and
the trail on the activity screen.

*(Deferred)*:

- More items in the menu.
- A key that opens the menu, such as F10. With only Quit in it, Ctrl+Q
  and the command palette already do what it does.
- The trail on the totals screen.

## Open questions

None.

## Architecture

### `TopBar` in `src/Lib/Widget`

A new widget, `TopBar.h`/`TopBar.cpp`, as nothing in it is about
activities. It's in a file of its own rather than next to `StatusBar` in
`Widget.h`, as it opens a `PopupMenu`, which `Widget.h` can't include.
`TopBar` joins the Widget group in `.clang-format`.

```cpp
// One line at the top of a window: a menu button at the left, and next
// to it a breadcrumb trail, whose crumbs can be clicked.
struct TopBar : public Widget {
    std::vector<MenuItem>       menu { };  // What the button opens
    std::vector<std::string>    trail { }; // Shown separated by "›"
    std::function<void(size_t)> on_crumb { };
    // Where draw() drew the button and the crumbs, in screen coordinates.
    Rectangle              button_area { };
    std::vector<Rectangle> crumb_areas { };

    explicit TopBar(pWidget const &parent);
    // Where the menu opens: under the button, at its left edge.
    [[nodiscard]] Vector2 menu_point() const;
    // The crumb at `mouse`, and whether `mouse` is on the button.
    [[nodiscard]] std::optional<size_t> crumb_at(Vector2 mouse) const;
    [[nodiscard]] bool                  on_button(Vector2 mouse) const;
    void draw() override;
    void process_input() override;
    // Over the button or a crumb, the pointing hand. A click on the
    // button opens the menu, on a crumb calls on_crumb.
    void handle_input(ScrollInput const &input);
};
```

- Its policy is `SizePolicy::Characters`, 1.0, and its background
  DARKGRAY, as the status bar's.
- **The button is drawn as three lines**, not as "☰": U+2630 isn't in
  the characters the app loads its fonts with (`App::font_has()`), and
  most of the bundled fonts don't have it. The button is a square the
  height of the bar. "›" (U+203A) is in General Punctuation, which the
  fonts load.
- The crumbs are RAYWHITE, the separators LIGHTGRAY. The one under the
  mouse gets a lighter background, so it reads as clickable.
- It works out where things are in `draw()`, which measures text, and
  `handle_input()` uses that, as `StatusBar`'s hint does; a test sets
  the areas by hand.
- The menu opens with `popup_menu(menu_point(), menu)`. A click under a
  modal never reaches the bar: `App::process_input()` gives input only
  to the modal then. So "the clicks do nothing while a modal is open"
  needs nothing of its own.

### The trail's text (`widget/ActivitySelector`)

Next to `year_entry()` and `month_entry()`, so that the crumbs name the
year and month as the lists do:

```cpp
// The top bar's trail to `shown`: "Activities", "2025", "November", or
// just "Activities" without an activity.
[[nodiscard]] std::vector<std::string> activity_trail(std::optional<Month> const &shown);
```

### The app (`SweatTrails`)

- `top_bar` is added first in `initialize()`, above `widget_stack`, so
  the window is the bar, the screen and the status bar, top to bottom.
- `menu` is one item, Quit, which submits `st-quit`: `PopupMenu` closes
  before running it, so `cmd_quit()`'s guard against a modal lets it
  ask.
- `SweatTrails::process_input()` sets `top_bar->trail` each frame:
  empty on the totals screen, else `activity_trail()` of the activity
  on display (`activity_display->activity`). Each frame, as the activity
  on display changes in many places (showing one, a recording, deleting,
  loading again after grouping, switching screens), and it's three
  short strings.
- `on_crumb` calls `open_crumb(sweattrails, ix)`, with the activity on
  display:
  - 0, Activities: `show_years()` with its year, as Ctrl+Y; without an
    activity the year is 0, so the latest is selected, and an empty
    store gets the message.
  - 1, the year: `show_year()` with its month selected.
  - 2, the month: `list_month()` with the activity selected.

  Each returns while a modal is open, as Ctrl+Y does, although the bar
  can't be clicked then: so that it holds whoever calls it.

## Test plan

- A new `top_bar_test` (`test/TopBarTest.cpp`), with a `ScopedTestApp`
  and areas set by hand: `crumb_at()` finds each crumb and nothing
  between or outside them; `on_button()`; `menu_point()` is under the
  button's left edge; `handle_input()` calls `on_crumb` with the crumb
  clicked, not without a press, not on the button or outside, and not
  for a crumb area left over from a longer trail.
- `ActivitySelectorTest`: `activity_trail()` with a month, and without.
- By hand, in the app: the bar on both screens, the trail following the
  activity on display (Left and Right across a month and a year), each
  crumb's list and selection, Activities with no activity on display,
  the menu's Quit asking, the pointing hand over the button and the
  crumbs, and the bar not taking clicks under a modal.

## Review notes

1. Suggestion: `open_crumb()` (`src/App/SweatTrails.cpp`, case 1) passes
   `std::nullopt` to `show_year()`, which then selects the month of
   `sweattrails->last_activity`, not of `activity_display->activity`, which
   the trail names. When the two differ (for instance a recording is
   `last_activity` while another activity is on display), the year crumb
   opens the list with a month other than the one in the trail. Passing
   `shown->month` as `select` makes the crumb match the spec ("the month on
   display selected") in every case.
2. Suggestion: `TopBar::button_hovered` and `crumb_hovered` are only updated
   in `process_input()`, which doesn't run under a modal. After a click that
   opens the menu or a list, the button or crumb stays lit behind the modal,
   and until the mouse moves once the modal is closed. Clearing both when
   the click is handled (or in `draw()` when a modal is open) avoids it.
3. Suggestion: the test plan's "not on the button or outside" is covered, but
   a click on the button with an empty `menu` and the `on_crumb` unset paths
   aren't. They are trivial branches; fine to leave.

The rest matches the spec: the bar is one line at the top on both screens,
the trail is empty on the totals screen, `activity_trail()` is tested, the
README is updated, and `crumb_at()` ignores stale areas. No bugs found. I
added no tests. The full test run passes (83/83).

### How the findings were resolved

1. Fixed (4b0da31), not by passing `shown->month`, which selects
   nothing if that month isn't in the counts, but by having `show_year()`
   take the month on display from `activity_on_display()` rather than
   `last_activity`, as its comment already said. It falls back to the
   year's latest month through `month_to_select()` as before. No test
   reaches `show_year()`, which is app code.
2. Fixed (bfc5b4e): `handle_input()` clears the hover when it handles a
   click. `test_click_clears_hover` in `top_bar_test` fails without it.
3. No change, as Jan decided.

Also, at Jan's request and outside this spec (a6a0390): `ListBox` shows
the pointing hand over an entry, and `PopupMenu` over an item that can
be chosen, tested in `listbox_test` and `popup_menu_test`.
