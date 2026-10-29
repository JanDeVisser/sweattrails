# Choosing an activity

Status: In review

## Goal

Finding the activity to look at is quicker. The list shows enough of
each activity to tell them apart, moving through the months doesn't
stop on months without activities, and the activities before and after
the one on display are a key away, without opening the list.

## Feature

### What there is now

Ctrl+M opens a list of a month's activities, starting with the month of
the activity on display. Each row is the start time and the title,
sorted as text. Left and Right go to the previous and next month, also
to months without activities. Typing filters the month's rows by their
text, Enter shows the activity, and Escape closes the list. Ctrl+M
isn't in the README.

Listing a month already reads each activity's summary (its sport,
distance and times), so a row can show more without reading more.

### The list's rows

```
🚲  Tue 14  09:32  Morning ride along the river        42.3 km  1:32:10
🏃  Thu 16  18:05  Intervals                            8.1 km    41:07
```

(The emoji stand in for the app's own sport icons.)

- Each row has the sport's icon, the day of the week and the day, the
  start time, the title, the distance and the moving time. The distance
  and the time are in columns on the right, lined up, and formatted as
  the data panel formats them. An activity without a distance, such as
  one indoors, leaves its distance empty.
- A title too long for the row is cut short; the columns aren't.
- The rows are in order of their start time, the oldest at the top, as
  in a calendar.
- Typing filters by the title only: typing a number would otherwise keep
  rows by their dates, distances or times that weren't meant.
- The icon and the columns are drawn by the list box, which the widget
  library's `ListBox` gains a way to do: a row with an icon and
  right-aligned columns besides its text. Other lists can use it too.

### Getting around the months

- Ctrl+M opens the list at the month of the activity on display, with
  that activity selected, rather than at the top of the month.
- **Left and Right** go to the nearest month before or after that has
  activities, skipping those that don't. At the first or the last month
  with activities they do nothing.
- **Shift+Left and Shift+Right** go a year back or forward: to the same
  month, or, if it has no activities, to the nearest one with activities
  from there in the same direction. If there's none, they do nothing.
- Moving to another month selects its first activity, as now.
- With no activities at all, Ctrl+M says so in a message, rather than
  opening an empty list of January of year 0, as it does now.

### The previous and next activity

- On the main screen, **Left and Right** show the activity before or
  after the one on display, by start time, also in another month,
  skipping months without activities. They do nothing at the first and
  the last activity, and with nothing on display.
- They match Up and Down, which go through the laps, and Left and Right
  in the list, which go through the months.
- Showing another activity this way is as choosing it in the list: the
  lap and the graph's selection start afresh.
- They're tasks, as every action is, so that they can be bound and run
  as the others. Left and Right in the list are the list's own keys, as
  its Up and Down are.

### README

The README says how to choose an activity: Ctrl+M and the list's keys,
and Left and Right on the main screen.

### Version 1 scope

In: the rows with an icon, the day and time, the title, the distance and
the moving time, oldest first; filtering by title; Left and Right
skipping months without activities; Shift+Left and Shift+Right for a
year; opening with the activity on display selected; a message when
there are no activities; Left and Right for the previous and next
activity on the main screen; the README.

*(Deferred)*:

- Searching all months by title.
- A key to go straight to the latest activity.
- Sorting the list by something other than the start time.

## Open questions

None.

## Architecture

Three parts: the list box in the widget library learns icons, a prefix
and columns; the storage finds the months with activities and an
activity's neighbours; and the app's selector and two new tasks use
them.

### The list box (`src/Lib/Widget/Modal.h`)

`ListBox::ListBoxEntry` gets three members, all empty by default, so
the other lists don't change:

```cpp
struct ListBoxEntry {
    std::string              text;         // What a search matches
    Payload                  payload;
    std::string              icon { };     // An image file, drawn before the row at its height
    std::string              prefix { };   // Drawn before the text, not searched
    std::vector<std::string> columns { };  // At the row's end, right-aligned, lined up across rows
};
```

- The columns are as wide as their widest entry among those shown;
  the text is cut short to leave room for them.
- The list box loads each icon once, the first time it's drawn, and
  unloads them when it goes.
- `select(Payload const &)` selects the entry with that payload, if it's
  shown, and scrolls to it: for opening at the activity on display.
- All strings, so the entries still compare for sorting. The selector
  doesn't sort (`ListBoxConfig { .sort = false }`): it adds the rows in
  the order of their start time, which `Month::list()` already sorts by.

### The months (`storage/`)

- **`Storage::months_with_activities() const`**: the months with an
  activity file, oldest first, read from the directories. It doesn't
  use or change `years`, which only jobs may touch, so the selector and
  the tasks can call it from the main thread. `rescan()` is rebuilt on
  it, so the two can't disagree.
- Pure functions on that list, in `storage/Types`, beside `Month`:

  ```cpp
  // The nearest month in `months`, oldest first, after or before `from`.
  std::optional<Month> month_after(std::span<Month const> months, Month const &from);
  std::optional<Month> month_before(std::span<Month const> months, Month const &from);
  // `from` moved by `years`, or the nearest month in `months` past that,
  // going the same way.
  std::optional<Month> years_from(std::span<Month const> months, Month const &from, int years);
  ```

  with `Month::plus(int months)` for the arithmetic across years.
- **`Storage::activity_next_to(ActivityID const &, int step)`**, not
  `const`, as `Month::list()` takes the storage to load from:
  the activity before (`-1`) or after (`+1`) by start time. In the same
  month if there is one, else the last or the first of the nearest month
  before or after whose list isn't empty: a month whose files all fail
  to load is passed over, as the list leaves them out. None at the ends.

### The app

- **`activity_entry(Activity const &)`** in `widget/ActivitySelector`
  makes a row: the icon file from `sport_icon_file()`, the prefix
  `"Tue 14  09:32"`, the title as the text, and the columns
  `format_distance()` and `format_moving_time()` from the data panel,
  the distance empty without one.
- `ActivitySelector` takes the months with activities, and moves with
  `month_after()`, `month_before()` and `years_from()`, which
  `destination(modifier, key)` picks between: a test can't press the
  keys, as moving calls `refresh()`, which measures the prompt with a
  font. It opens at the month of the activity on display, else the
  latest, and selects it.
- `format_distance()` and `format_moving_time()` are declared in
  `widget/DataFields.h` for it, rather than kept inside the data panel.
- The list box cuts a title by characters rather than bytes, so that a
  title with an `é` at the cut doesn't become invalid UTF-8.
- `cmd_list_month()` shows a message when there are no months with
  activities.
- **`st-previous-activity` and `st-next-activity`**, bound to Left and
  Right, call `activity_next_to()` and show what it finds, as choosing
  it in the list does. Like the list, they load on the main thread.

## Test plan

- `ListBoxTest`: the new members don't change a list without them;
  `select()` selects and scrolls, and does nothing for a payload that
  isn't shown; searching matches the text, not the prefix or columns.
- `StorageTest` (or a new `MonthsTest`) with a store of real FIT files
  from `TestFit.h`:
  - `months_with_activities()`, skipping empty month directories;
  - `month_after()`, `month_before()`, `years_from()`, across years and
    at the ends, and `Month::plus()`;
  - `activity_next_to()` in a month, across months over an empty one,
    past a month whose file doesn't load, and at the ends.
- `ActivitySelectorTest`: `activity_entry()` for a ride and for one
  without a distance; Left and Right skip empty months; Shift+Left and
  Shift+Right a year; opening selects the activity; typing matches the
  title only.
- By hand: the list's icons and columns, at a few window sizes, and
  Left and Right on the main screen.

## Review notes

*Filled in by the Reviewer.*
