# Gear

Status: In review, on branch feature/67-gear

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

Keep track of the gear used for activities, as Strava does, but in more
detail: running shoes, and bikes with their components (chainrings,
wheels, cassettes, chains), so that the distance on each is known, for
example to know when a pair of shoes or a chain is worn out, or when the
chain needs waxing again.

## Feature

### What Jan asked for

Running: shoes, per pair:

- Brand, model, model number, colourway. The model number is the
  model's version, as makers bring out a new one most years: the 5 of
  a Saucony Endorphin Speed 5. It's `version` in `gear.json`, text, and
  shown after the model. The maker's article code isn't kept.
- Date purchased, where, price
- Mileage

Cycling: bikes and some of their components:

- Brand, model and model year of the bike
- Total mileage
- A standard chainring setup, with its mileage. A different chainring
  setup can be set per activity.
- Standard wheels with a standard cassette. Different wheels can be
  selected per activity, and a different cassette for the wheels.
- Kilometres on the chain
- Kilometres since the chain was last waxed
- Mileage for chainsets and cassettes

### What there is to build on

- **Per-activity data goes in the sidecar**, `<name>.json` next to the
  FIT file, under SweatTrails' own keys, with `update_sidecar()`
  (`storage/Activity.h`), as the user's title, notes and laps do
  (`specs/11-edit-activity-name.md`, `specs/41-edit-description.md`,
  `specs/26-after-the-fact-laps.md`). The gear used for an activity can
  go there too.
- **Every activity's distance and sport is already summarized** in
  `summaries.json` (`storage/Summaries.h`, `specs/27-volume-summaries.md`),
  kept up to date by a background job. Mileage on a piece of gear is a
  sum over those, so no FIT file has to be read for it.
- **Distances are metric** throughout the app: gear distances are in km.
- **No form widget yet**: there's `TextLine`, the `InputBox`, `ListBox`
  and `PickBox` modals, `Table` and `TabStrip`, but nothing that edits a
  record of a dozen fields, nor a date or a number field.
- **Nothing about gear yet** anywhere: not from Strava (its export has
  `bikes.csv` and `shoes.csv`, and the API an activity's `gear_id`), nor
  from Garmin, nor from the FIT files' `bike_profile` messages.

### Gear is entered by hand, in `gear.json`

The gear is in `gear.json` in the data directory, which Jan edits by
hand, as the settings are. The app reads it, shows the gear and the
distance on each, and lets the user pick the gear of an activity. A form
to enter and edit gear in the app is *(deferred)*: a form of a dozen
fields, with dates and a price, is a widget the library doesn't have,
and the biggest cost in the feature. It can come once it's clear what it
needs.

### Mileage is computed, not entered

A piece of gear's distance is the sum of the distances of the
activities it was used for, plus a starting distance for what it did
before SweatTrails knew about it. It isn't a number the user types and
keeps up to date.

### `gear.json`

```json
{
  "shoes": [
    { "name": "Pegasus blue", "brand": "Nike", "model": "Pegasus",
      "version": "41", "colourway": "Blue",
      "purchased": "2026-03-14", "where": "Running Room", "price": "179.99",
      "start_km": 0, "retired": "2026-11-01" }
  ],
  "bikes": [
    { "name": "Canyon", "brand": "Canyon", "model": "Ultimate CF SL", "year": 2022,
      "purchased": "2022-05-01", "where": "canyon.com", "price": "3200",
      "start_km": 4300 }
  ],
  "defaults": [
    { "for": "running", "gear": "Pegasus blue", "from": "2026-03-14" },
    { "for": "road", "gear": "Canyon", "from": "2022-05-01" },
    { "for": "indoor", "gear": "Canyon", "from": "2025-11-01" },
    { "for": "indoor", "gear": "Gravel", "from": "2026-04-01" }
  ]
}
```

- **`name` is the gear's key**: unique over shoes and bikes, what the app
  shows, and what an activity's sidecar refers to. There's no hidden id.
  Renaming gear in `gear.json` leaves the activities picked under the
  old name pointing at gear that isn't there; the app says so (see
  below) rather than dropping them.
- **All other fields are optional.** Dates are `YYYY-MM-DD`. `price` is
  free text, so a currency can be written with it. `start_km` is the
  distance the gear did before SweatTrails knew about it, 0 if left
  out. `retired` is the date it went out of use.
- **The defaults are a list of dated entries**, not a flag on the gear,
  because the bike on the trainer changes with the season: Jan rides
  his road, gravel or TT bike on Zwift depending on the time of year.
  An entry says that from its date on, gear is the default for a kind
  of activity, until a later entry for the same kind. Buying new shoes
  means adding an entry.

### Which gear an activity uses

- **A pick in the activity's sidecar wins.** Picking gear for an
  activity saves its name there, under SweatTrails' key `"gear"`.
- **Otherwise the default for its kind on its date**: the latest entry
  in `defaults` for the activity's kind that's dated on or before the
  activity's day. No entry, no gear.
- **The kinds** come from the activity's sport and sub-sport, most
  specific first:
  - `running`: every kind of run (road, trail, treadmill, track).
  - `road`, `gravel`, `mountain`, `tt` (time trial): outdoor rides of
    that sub-sport, falling back to `cycling` without an entry for it.
  - `indoor`: indoor cycling and virtual rides (Zwift), falling back to
    `cycling`.
  - `cycling`: every other ride, and the fallback above. Many files
    don't set a sub-sport, so a default for `cycling` is what most
    rides fall back to.
  - Walking and hiking have no kind: shoes count for them only when
    picked. The developer maps the FIT sub-sports onto these kinds.
- **Retired gear** isn't offered when picking, and a default entry that
  names gear retired before the activity's day doesn't apply.
- **Multisport activities have gear per leg**: shoes for the run, the
  bike for the ride, with the same rules per leg. A leg's pick goes in
  the leg's part of the sidecar, as its title does
  (`specs/58-multisport-leg-titles.md`).

### Reading `gear.json`

- **Read at startup, and again each time the Gear screen or the picker
  opens**, so an edit shows without restarting the app. The file is
  small.
- **No `gear.json`** is no gear: the Gear screen says how to make one,
  and the picker says there's no gear.
- **A `gear.json` that can't be read**, or that has two pieces of gear
  of the same name, or a default naming gear that isn't there, is an
  error on the status line, as a broken settings file is
  (`specs/47-status-bar.md`). The gear read before stays in use.

### Gear on the activity screen

- **One line in the activity's info box**, `Gear: Canyon`. Gear that
  comes from a default, not a pick, is shown dimmed, so a guess can be
  told from a choice. No line when the activity has no gear.
- **A pick naming gear that isn't in `gear.json`** is shown as it is,
  `Gear: Old Canyon (unknown)`.
- **For a multisport activity**, the line is the gear of the leg on
  display, as the title is.

### Picking gear

- **`st-gear`, "Gear: pick for this activity"**, in the command palette,
  and by clicking the gear line in the info box. No key of its own:
  it's not something done often enough to spend a key on, and the
  palette finds it. With no gear line, as for an activity without
  gear, the palette is the way.
- **It opens a `PickBox`** with the gear that fits the activity: shoes
  for a run, bikes for a ride, both for anything else, such as a walk or
  a hike. Retired gear is left out, unless it's the activity's current
  pick.
- **Two entries besides the gear:**
  - `Default (Canyon)`, or `Default (none)`: removes the pick from the
    sidecar, so the default applies again.
  - `None`: no gear for this activity, such as a run in borrowed shoes.
    Saved as `"gear": null`, which a default doesn't override.
- **The current choice is selected** when the box opens.
- **Saving** is `update_sidecar()`, as for the title; an activity
  without a sidecar gets one. An error saving is on the status line.
- **For a multisport activity**, the pick is for the leg on display.

### The Gear screen

- **A third screen**, next to the activity screen and the totals
  (`SummariesScreen`), opened from the hamburger menu (`Gear`) and the
  command palette (`st-gear-list`, "Gear: list"). Leaving it is as
  leaving the totals.
- **A `Table`, a row per piece of gear**, shoes first, then bikes:
  name, brand and model, in use since (`purchased`), the number of
  activities, and the distance, `start_km` plus the activities', in km.
  Retired gear is at the bottom of its group, dimmed.
- **The distance comes from `summaries.json`**: an activity's distance,
  or its leg's. While the summaries are being brought up to date the
  screen shows what there is, and is updated when they are, as the
  totals are.
- **Names in sidecars that match no gear** get a row each, `Unknown:
  Old Canyon`, with their activities and distance, so that fixing a
  rename in `gear.json` is seen to bring them back.
- Clicking a row to list that gear's activities is *(deferred)*.

### Version 1 scope

The feature is split in two specs:

1. **This spec, Gear**: shoes and bikes as whole items, which one is
   used for each activity, and the distance on each.
2. **A follow-up, Bike components**: chainrings, wheels, cassettes and
   chains on a bike, swaps per activity, and maintenance such as chain
   waxing. It builds on this one, and is written once this one is Done.

Components with swaps and maintenance is at least as big as the rest,
and a feature on its own.

Deferred from this spec:

- A form to enter and edit gear in the app.
- Listing a piece of gear's activities from the Gear screen.
- Importing gear from Strava, its export (`bikes.csv`, `shoes.csv`, an
  activity's gear in `activities.csv`) or its API (`gear_id`): useful
  for the history, but its names and dates need mapping onto the gear
  in `gear.json`.
- Gear from Garmin Connect, or from the FIT files' `bike_profile`.

The README says how to write `gear.json`, with the example above.

## Open questions

None.

## Architecture

### Gear and its rules (`storage/Gear.h`, `.cpp`, new)

Everything that decides which gear an activity uses, and the distance on
each, is here, as pure functions a test reaches without a window.

- `struct Gear`: `type` (`GearType::Shoes` or `Bike`), `name`, `brand`,
  `model`, `version`, `colourway`, `year` (optional), `purchased`,
  `retired` (optional `year_month_day`), `where`, `price` (text),
  `start_km`.
- `GearKind`, an X-macro with names: `running`, `road`, `gravel`,
  `mountain`, `tt`, `indoor`, `cycling`.
- `struct GearDefault { GearKind kind; std::string gear; year_month_day from; }`.
- `struct GearList { std::vector<Gear> gear; std::vector<GearDefault> defaults; }`,
  shoes before bikes, with `find(name)`.
- `decode_gear(JSONValue) -> std::expected<GearList, std::string>`, as
  `decode_thresholds()`: an error for a field of the wrong type, a date
  that isn't `YYYY-MM-DD`, a kind it doesn't know, two pieces of gear of
  the same name, or a default naming gear that isn't there.
  `read_gear(path) -> std::expected<std::optional<GearList>, std::string>`:
  none without the file. `gear_path(Storage)` is
  `storage.data_dir / "gear.json"`, next to `summaries.json`.
- `gear_kinds(sport, sub_sport) -> std::vector<GearKind>`, most specific
  first. The FIT sub-sports:
  - Sport running, or sub-sport `treadmill` or `indoor_running`:
    `running`.
  - Sub-sport `indoor_cycling`, `spin`, or cycling with
    `virtual_activity`: `indoor`, `cycling`.
  - Cycling with `road`: `road`, `cycling`. `gravel_cycling`,
    `cyclocross`, `mixed_surface`: `gravel`, `cycling`. `mountain`,
    `downhill`, `e_bike_mountain`: `mountain`, `cycling`. Any other
    cycling: `cycling`.
  - Anything else, walking and hiking too: none.
  - FIT has no time-trial sub-sport, so nothing maps onto `tt`. It's
    accepted in `gear.json`, and the README says a TT bike is picked.
- `default_gear(list, kinds, day) -> std::optional<std::string>`: for each
  kind in order, the latest entry for it dated on or before `day`; the
  first kind with one wins. If that entry's gear was retired before
  `day`, there's no gear: neither an earlier entry nor the fallback
  kind takes its place, so a retirement ends the default, and the gap
  until a new entry shows as activities without gear.
- `struct ActivityGear { std::string name; bool from_default; bool unknown; }`
  and `activity_gear(list, pick, kinds, day) -> std::optional<ActivityGear>`:
  the pick, else the default; none for `"gear": null`. `unknown` for a
  pick naming gear that isn't in the list.
- The picker's entries, `gear_choices(list, kinds, pick, day)`: a
  `GearChoice` per line, its text and the `std::optional<GearPick>` to
  save (none for Default). Shoes for `running`, bikes for a cycling
  kind, both without kinds; gear retired before the activity's day left
  out unless it's the pick, so an old activity can still get the gear
  it was done with. A pick of gear that isn't in the list gets a line
  of its own, `Old Canyon (unknown)`. And which line is the current
  choice.
- The Gear screen's numbers: `GearPicks`, the picks in the sidecars,
  by summary file and leg; `read_gear_picks(Storage, summaries)`, which
  reads the sidecar of each summary that has one, for the job; and
  `gear_totals(list, summaries, picks) -> std::vector<GearTotal>`
  (name, unknown, retired, count, km), shoes then bikes, retired last
  in their group, then the unknown names.
### The activity (`storage/Activity.h`, `.cpp`)

The sidecar's part of the gear is here rather than in `storage/Gear`, so
that the many tests that link `Activity.cpp` don't all need `Gear.cpp`.

- `struct GearPick { std::optional<std::string> name; }`: the `"gear"`
  key of a sidecar part. No `GearPick` is no key; a `GearPick` without
  a name is `"gear": null`. `sidecar_gear(part)` reads it; anything but
  a non-empty string or null is no pick.
- `save_sidecar_gear(sidecar, std::optional<GearPick>, leg)`, with
  `update_sidecar()` and `change_sidecar_part()`, as the title.
- `SidecarTitles` and `LegTitles` get `std::optional<GearPick> gear`, read
  in the same read of the sidecar as the names.
- `Activity` and `Leg` get `std::optional<GearPick> gear_pick`, which
  `read_names()` sets: the activity's, the legs' for a multisport
  activity (whose whole has none), and `leg_activity()` passes the
  leg's on to its tab. `Activity::set_gear(std::optional<GearPick>)`
  saves it as `rename()` does, the leg's on a leg's tab.

### The widget library

- `InfoBox` (`src/Lib/Widget/InfoBox.*`): `InfoRow` gets `dim`, drawn in
  the small text's grey. More than one row can be a link:
  `click_link(size_t row, double now)` gets the index of the row
  clicked, and `draw()` keeps a link area per link row. The activity
  info box's file row and gear row are both links.
- `Table` (`src/Lib/Widget/Table.*`): `std::vector<bool> dimmed`, set with
  the rows, drawn in grey: retired gear.
- `PickBox` (`Modal.h`) takes a `ListBoxConfig`, as `ListBox` does, and
  `list_box<Config>()` passes one on, so that the gear picker keeps its
  order (Default, None, then the gear as in the file). Its default still
  sorts. The picker's payload is the line's index into the
  `GearChoices`, as a `ListBox` orders entries by their payloads.

### The app

- `ActivityInfo` gets `std::optional<ActivityGear> gear`, and
  `describe()` the gear list. `info_rows()` adds a `Gear` heading with
  the gear under it, `Canyon`, dimmed for a default, or
  `Old Canyon (unknown)`, as the box's other sections are, rather than
  one `Gear: Canyon` line. The overview of a multisport activity has
  no gear line, and `st-gear` there says on the status line to pick
  gear on a leg's tab. `ActivityInfoBox` takes a fifth callback, `PickGear`, which
  a click on the gear row calls.
- `widget/Gear.h`, `.cpp` (new): `gear_columns()`, `gear_rows(totals)` and
  `gear_heading(...)` as text, and `GearScreen : Layout`, a heading and a
  `Table`, as `SummariesScreen` has. `set_gear(list)`, `set_summaries()`
  and `set_picks()` work the totals out again.
- `SweatTrails`: `std::optional<GearList> gear` (none without the file),
  `read_gear_file()` (at startup, and when the screen, the picker or
  the activity info box opens; an error on the status line, keeping the
  gear before), and `gear_screen` in the widget stack. `showing_gear()`,
  and `showing_activity()` in place of the `!showing_summaries()` that
  stand for "on the activity screen".
- A job, `ReadGearPicks`, reads the sidecars' picks when the Gear screen
  opens (6,000 sidecars, half a second), of the summaries it's given,
  and its notification, `GearPicksRead`, hands them to the screen.
  Until then the screen lists the gear without numbers. A pick made in the app
  while the screen is closed is read with the rest next time.
- Commands: `st-gear` ("Gear: pick for this activity", on the activity
  screen), `st-gear-list` ("Gear: list", and the hamburger menu's
  `Gear`), `st-gear-close` (Esc on the Gear screen), and `st-gear-row`
  (Up and Down through its rows). The picker is a
  `list_box()` whose payload is the `std::optional<GearPick>` to save;
  saving calls `Activity::set_gear()` on the activity on display, as
  renaming does.
- The README gets a section on `gear.json`. Its example adds the
  `Gravel` bike that the spec's example's defaults name but its `bikes`
  leave out: as it is above, the app says the file can't be read. No new dependency, and
  `.clang-format`'s groups already cover `storage/` and `widget/`.

## Test plan

- `gear_test` (new), on `storage/Gear`:
  - `decode_gear()`: the spec's example; optional fields left out;
    `start_km` 0 by default; each error: a bad date, an unknown kind, a
    duplicate name across shoes and bikes, a default naming unknown
    gear, a field of the wrong type. `read_gear()` of no file is none.
  - `gear_kinds()` for each mapped sub-sport, a run, a walk, a ride
    without a sub-sport.
  - `default_gear()`: the latest entry on or before the day, an entry
    dated the day itself, the season's switch of the indoor bike,
    falling back from `indoor` to `cycling`, no entry before the day,
    retired on the day kept; gear retired before the day gives no gear,
    though an earlier entry, or one for the fallback kind, names gear
    that isn't retired.
  - `activity_gear()`: a pick wins over a default, `"gear": null` is
    none, a pick of unknown gear is shown as unknown.
  - `gear_choices()`: shoes for a run, bikes for a ride, both for a
    walk; retired gear left out unless picked; Default's text with and
    without a default; the current choice for a pick, for null, for no
    pick.
  - `gear_totals()`: picks and defaults counted, `start_km` added, legs
    of a multisport file counted on their own gear, retired last,
    unknown names in rows of their own.
  - `save_sidecar_gear()` in a temporary directory: a pick, null, back to
    default (the key removed, and the sidecar with it if empty), a
    leg's; and the activity reads it back through `sidecar_titles()`.
- `activity_info_box_test`: the gear row, dimmed for a default, and a
  click on it calls `PickGear`; the file row still copies.
- `gear_screen_test` (new): `gear_rows()` and its dimmed rows; the heading
  without `gear.json`.
- `table_test` (or `listbox_test`'s neighbour): `dimmed` follows
  `set_rows()`.
- By hand: write `gear.json`, see the info box's line, pick gear and
  see the sidecar, open the Gear screen, break the file and see the
  status line.

## Review notes

1. **Bug**: `st-delete-activity` (`SweatTrails.cpp`, its `.when()`, near
   "Delete activity") still says `!showing_summaries()`, one of the
   replacements by `showing_activity()` that was missed. On the Gear
   screen the palette offers "Delete activity", and it deletes the
   activity behind the screen, which the user can't see. It should be
   `showing_activity() && activity_display->activity != nullptr`.
2. **Suggestion**: `notif_summaries_updated()` calls
   `gear_screen->set_summaries()`, which runs `gear_totals()` over every
   summary (thousands) on each update while the Gear screen is closed,
   and the update job notifies per chunk. `GearScreen::refresh()` could
   do nothing until `open()`.
3. **Suggestion**: `ReadGearPicks` results aren't tied to an opening of
   the screen. Close and reopen the screen while the first job runs: the
   first job's `GearPicksRead` clears `reading` and shows its numbers,
   and the second replaces them. Harmless, but the picks of the first
   may be older than a pick made in between.
4. **Suggestion**: `decode_gear()` ignores unknown top-level keys, so a
   typo such as `"shoe"` or `"default"` is no gear and no error, with
   nothing on the status line.
5. **Missing tests (added)**: `gear_test` gets the defaults in any order
   (and two on one day: the later in the file wins), the totals with a
   default naming gear retired before the day, and `gear_kinds()` for a
   walk on a treadmill, a walk, a hike and no sport. All pass.

Checked and fine: the sub-sport mapping in `gear_kinds()`;
`default_gear()` with sorted defaults and retirement; a multisport file
counted once per piece of gear; leg picks read back after `show_tab()`
(`read_names()` refreshes them, so `set_gear()`'s no-change shortcut
isn't fooled by a stale copy); the info box's `weak_ptr` holder; the row
index of the gear link; the day of a summary and of an activity are the
same `DateTime`.
