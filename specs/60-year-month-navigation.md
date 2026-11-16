# Getting around by year and month

Status: In progress

## Goal

Getting to an activity a long time ago is quick. Now there's Left and
Right on the main screen, for the activity before and after, and Ctrl+M,
for the list of a month's activities, which moves a month at a time with
Left and Right, or a year with Shift+Left and Shift+Right. Getting to an
activity three years back takes a lot of key presses, and there's no
overview of which years and months have activities.

With this feature, the store can be browsed as a tree: the years, the
months of a year, and the activities of a month, with keys to go down
and up between them.

## Feature

### The three lists

- **Ctrl+Y** opens the list of the years with activities, oldest at the
  top, with the year of the activity on display selected. With nothing
  on display, the latest year is selected.
- **Enter on a year** shows the list of the months of that year with
  activities, in calendar order.
- **Enter on a month** shows Ctrl+M's list of that month's activities,
  as it is now.
- **Left and Right in the month list** go to the year before or after
  with activities, skipping years without. At the first or the last
  year they do nothing. This matches Left and Right in the activity
  list, which go through the months.
- **Cmd+Up on macOS, Ctrl+Up elsewhere** goes up: in the activity list,
  to the month list of its year, with its month selected. In the month
  list, to the year list, with its year selected. In the year list, it
  does nothing. Not Ctrl+Up on macOS, which is Mission Control by
  default, so the app never sees it. Cmd+Up is Finder's "enclosing
  folder", the same idea, and the app already uses Cmd on macOS where
  others use Ctrl (Cmd+Shift+R). Not Backspace with an empty filter,
  which is easy to press by accident when clearing one.
- Going down, up, or to another year replaces the list in its box: there
  is only ever one list open. **Escape** closes it, whichever level it's
  on, as it closes the activity list now; going up has its own key.
- Up, Down and Enter work as in any list. Typing doesn't filter the year
  and month lists: with 12 rows at most, it's of little use.
- Ctrl+Y does nothing while a modal is open, as Ctrl+M doesn't.
- With no activities at all, Ctrl+Y says so in a message, as Ctrl+M
  does.

### The rows

Each row is the year or the month's name, with the number of activities
in it right-aligned, in a column as the activity list has them:

```
2024      187
2025      203
```

```
January    14
March      17
```

The count is of the activities the activity list would show, so that it
matches what Enter leads to. Listing a month reads each activity's
summary, so counting the year list this way reads the whole store: the
Developer checks what that costs with a store of several years, and if
it's too slow for a key press, finds a cheaper count that still leaves
out what the activity list leaves out.

### Which month is selected

When the month list opens, from a year or from Left and Right:

- the month of the activity on display, if it's in that year;
- else the latest month of that year with activities.

Going up from the activity list selects the month that list was showing.

### Months that list nothing

A month directory with only a `.strava` file, or only FIT files that
don't load, lists no activities (see `21-activity-selection.md`). Such a
month isn't in the month list, and a year with only such months isn't in
the year list, so that Enter never leads to an empty list.

### README

The README describes Ctrl+Y and the keys of the three lists.

### Version 1 scope

In: Ctrl+Y and the year list; the month list; the number of activities
in each row; going down with Enter and up with Cmd+Up (Ctrl+Up off
macOS); Escape closing at any level; Left and Right in the month list;
the README.

*(Deferred)*: distance and time totals per year or month in the rows:
the Summaries screen has those.

## Open questions

None.

## Architecture

### What counting costs

Counting with `Month::list()`, as the spec first asks, reads every FIT
file in the store. With Jan's store (5,941 activities in 18 years,
870 MB), in a Debug build, the year list's counts took 68 s, and the
month list of one year (2025, 380 activities) 7 s. Too slow for a key
press either way, also in a Release build.

So both lists count from the **summaries** the totals keep
(`storage/Summaries.h`, `SweatTrails::summaries`), which are in memory
on the UI thread. A summary without a sport is of a file that can't be
read, which the activity list leaves out too; a multisport file is one
activity, as `store_summary()` counts it. The summaries can lag behind
the store: on the first start, while the job reads them, and after a
download until it has read the new files. A FIT file in the store
without a summary yet is counted as an activity, as it most likely
loads: so a new month shows up at once, and a store whose summaries are
still being built is listed in full. The price is that a broken file
counts until its summary is read.

Both count only FIT files named by their start time, as the summaries
do. The activity list also lists one named otherwise, which only gets
there by hand, so a month with only such files is missing from the
month list. That leaves Enter never leading to an empty list.

### Counting (`storage/Summaries`)

```cpp
// The activities each month lists: by month, only months with any.
using MonthCounts = std::map<Month, size_t>;

// The months' activities from the summaries, leaving out files that
// can't be read and counting a multisport file once, and from `files`,
// the store's FIT files ("2025/03/1741000000.fit"), those without a
// summary. Pure, for the tests.
[[nodiscard]] MonthCounts month_counts(ActivitySummaries const &summaries, std::vector<std::string> const &files);
// The same, with the store's FIT files as activity_starts() finds them:
// from the directories, no file is read.
StorageResult<MonthCounts> month_counts(Storage const &storage, ActivitySummaries const &summaries);

// The year before (`step` -1) or after (+1) `year` with activities.
[[nodiscard]] std::optional<size_t> year_next_to(MonthCounts const &counts, size_t year, int step);
// The month the month list of `year` opens at: `shown`'s, if it's in
// that year and has activities, else the year's latest.
[[nodiscard]] std::optional<Month> month_to_select(MonthCounts const &counts, size_t year, std::optional<Month> const &shown);
```

`summary_activity()` is rebuilt on a `file_activity(std::string_view)`
that reads the month from a relative file name, which `month_counts()`
uses for both.

### The lists (`widget/ActivitySelector`)

Three lists, each its own modal, as `ActivitySelector` is now. Going
down, up or to another year dismisses the list and shows the next one
in its place: one list open at a time, and Escape closes whichever it
is, as `ListBox` already does. The lists don't know about each other:
the app gives each a function for Enter and one for going up.

```cpp
using YearList = ListBox<size_t, ListBoxConfig { .search = false, .sort = false }>;
using MonthList = ListBox<Month, ListBoxConfig { .search = false, .sort = false }>;

// "2024", with the count as a column; "January", likewise.
YearList::ListBoxEntry  year_entry(size_t year, size_t count);
MonthList::ListBoxEntry month_entry(Month const &month, size_t count);

struct YearSelector : public YearList {      // Ctrl+Y
    YearSelector(Submit const &submit, MonthCounts counts);
    // Cmd+Up is taken, and does nothing.
};

struct MonthSelector : public MonthList {
    MonthSelector(Submit const &submit, Up const &up, MonthCounts counts, size_t year, std::optional<Month> shown);
    // Left and Right: year_next_to(), then the rows of that year and
    // month_to_select(). destination() as ActivitySelector has it, as
    // moving calls refresh(), which needs a font.
    // Cmd+Up: up().
};
```

`ActivitySelector` gets an `Up` function too, for Cmd+Up; Ctrl+M's list
has one as well, so Cmd+Up works however it was opened.

The up key is `GoUpKey`, `KeyCombo { KEY_UP, KModSuper }` on macOS and
`KModControl` elsewhere, declared in `widget/ActivitySelector.h` as
`TextAreaBox.h` declares its save modifier.

### The app (`SweatTrails.cpp`)

- `st-list-years`, "List the years with activities", bound to Ctrl+Y.
  `cmd_list_years()` returns while a modal is open, says so in a message
  when there are no activities, and else shows the year list with the
  year of `last_activity` selected, or the latest.
- `list_month()` keeps its guard against an open modal, and the
  showing moves into a function the lists call on Enter on a month,
  which is called while the month list is still open.
- The functions for Enter and up compute `month_counts()` afresh,
  which is cheap: the summaries may have changed since the list opened.
- Nothing in the widget library changes: `ListBox` already has the
  columns and `select()`.

## Test plan

- `SummariesTest`: `month_counts()` counts readable summaries, leaves out
  one without a sport, counts a multisport file's legs once, counts a
  file without a summary, doesn't count a summary twice when its file
  is in `files` too, and leaves out months with nothing; `year_next_to()`
  skips a year without activities and does nothing at either end;
  `month_to_select()` with `shown` in the year, in another year, in a
  month of that year without activities, and none.
- `ActivitySelectorTest`:
  - `year_entry()` and `month_entry()`'s text and column;
  - the year list's rows, oldest first, and Enter submits the year;
  - the month list's rows in calendar order, with the right month
    selected; Enter submits the month; `destination()` for Left and
    Right, skipping a year, and at the ends;
  - Cmd+Up (Ctrl+Up off macOS) calls up from the month list and the
    activity list, and does nothing in the year list; Ctrl+Up on macOS
    doesn't.
- `month_counts(Storage const &, …)` with a `TempStore` of real FIT
  files from `TestFit.h`: a month with a FIT file, one with only a
  `.strava` file, which isn't counted.
- By hand, in the app: Ctrl+Y on Jan's store, down to an activity and
  back up, Left and Right in the month list, Escape at each level.

## Review notes

*Filled in by the Reviewer.*
