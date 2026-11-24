# Bike components

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

Know the distance on a bike's wearing parts, the chainset, wheels,
cassettes and chains, and the distance on a chain since it was last
waxed, so that it's clear when a part is worn out or a chain needs
waxing again. This is the second half of `specs/67-gear.md`, which
tracks shoes and bikes as whole items.

## Feature

### What Jan asked for (from spec 67)

- A standard chainring setup per bike, with its mileage. A different
  chainring setup can be set per activity.
- Standard wheels with a standard cassette. Different wheels can be
  selected per activity, and a different cassette for the wheels.
- Kilometres on the chain.
- Kilometres since the chain was last waxed.
- Mileage for chainsets and cassettes.

### What there is to build on

- **`gear.json`** (`storage/Gear.h`): shoes and bikes, keyed by a unique
  `name`, and `defaults`, dated entries saying which gear an activity of
  a kind uses from a day on. Hand-edited; the app only reads it, at
  startup and as the Gear screen, picker or info box opens. Unknown
  top-level keys are an error, so this spec adds the keys it needs.
- **Which bike an activity used** is settled: `activity_gear()`, a pick
  in the sidecar or the default for the activity's kind and day, per leg
  for multisport.
- **Distances come from `summaries.json`**, per activity or leg, with
  its sport and sub-sport. `read_gear_picks()` reads the sidecars' picks
  in a job for the Gear screen.
- **The Gear screen** (`widget/Gear.h`): a `Table`, a row per piece of
  gear, with activities and km.
- **The picker**: `st-gear`, a `PickBox` of `gear_choices()`, saving to
  the sidecar's `"gear"` key.

### Parts, and where they're fitted

*(Changed after the first round of development, 2026-11-24. The first
version had a top-level `parts` list and a dated `fitted` list naming
parts and places across the file; editing it by hand, Jan found it
awkward and error prone. Parts are now nested where they are, and the
dates are on the parts.)*

- **Parts are gear too**, with the same fields as a bike (name, brand,
  model, purchased, where, price, `start_km`, `retired`) and a `type`:
  `chainset`, `front wheel`, `rear wheel`, `cassette`, `chain` or
  `tyre`. Tyres were added to what spec 67 listed: they wear out as the
  rest do. The name is unique over shoes, bikes and parts, wherever
  they're nested.
- **Wheels are front and rear, not pairs.** Wheels come as a pair, or a
  front or a rear wheel on its own; modelling both pairs and single
  wheels is more than it's worth, so a pair is two parts.
- **A part is listed where it's fitted**: in a bike's `fitted` list, or
  a wheel's. Parts that aren't on anything, such as a spare race wheel,
  are in a top-level `unfitted` list, with what's fitted on them:

  ```json
  "bikes": [
    { "name": "Roadie", "brand": "Cervélo", "model": "S2", "year": 2018,
      "fitted": [
        { "name": "SRAM Force AXS 48/34 w/ Quarq", "type": "chainset" },
        { "name": "Flo45 front", "type": "front wheel",
          "fitted": [ { "name": "GP5000 Flo45 front", "type": "tyre" } ] },
        { "name": "Flo45 rear", "type": "rear wheel",
          "fitted": [ { "name": "GP5000 Flo45 rear", "type": "tyre" },
                      { "name": "SRAM AXS 10-30", "type": "cassette" } ] },
        { "name": "KMC old", "type": "chain", "retired": "2026-06-01" },
        { "name": "SRAM Force AXS Chain (S2)", "type": "chain",
          "since": "2026-06-01", "max_km": 3000, "wax_km": 300,
          "waxed": ["2026-11-20", "2026-08-16"] }
      ] }
  ],
  "unfitted": [
    { "name": "Flo Disk", "type": "rear wheel",
      "fitted": [ { "name": "GP5000 Flo Disk", "type": "tyre" },
                  { "name": "SRAM AXS 10-28", "type": "cassette" } ] }
  ],
  "swaps": [
    { "part": "Flo Disk", "on": "Roadie", "from": "2026-08-30", "until": "2026-08-30" }
  ]
  ```

  A chainset, chain or wheel is fitted on a bike, a cassette on a rear
  wheel, and a tyre on either wheel. In `unfitted`, any type can be
  listed, with a wheel's parts nested in it.
- **A part's `since`** is the day it was fitted where it's listed; rides
  there before it don't count for it. It's optional and defaults to
  `purchased`, as a part can't have been ridden before it was bought, so
  most parts don't need one. Without either, the part has always been
  there.
- **A replaced part stays in its place, with `retired`**, so a place's
  history is in the place. For a ride, a place has, of its parts of a
  type, the one with the latest `since` on or before the ride's day,
  unless that part was retired before that day. On the day of a
  replacement, the new part wins. A retired part with no replacement
  leaves the place without one, until there is. Old chains pile up under
  the bike over the years, which is accepted: it's what keeps the
  history.
- **A part that moves for good** (a wheel to another bike) is moved in
  the tree, gets a `since` for the day it moved, and its distance so
  far, read from the Gear screen, is added to its `start_km` by hand.
  This is rare. Without the `since`, it would quietly take the new
  bike's past rides.
- **What the tree can't say** is A, then B, then A again in one place,
  as a part has one `since`. If a part comes off for good and goes back
  on for good later, its first stint is lost unless it's added to
  `start_km`. Rare enough to accept.
- **Temporary swaps** are a top-level `swaps` list:
  `{ "part", "on", "from", "until" }`, with `until` required (a lasting
  change is made in the tree). From `from` to `until`, both inclusive,
  the part is on that place instead of the part of its type that's
  there, and takes its own nested parts along: a swapped-in rear wheel
  brings its cassette and tyre. The part's own place, if it has one, is
  without it for those days. This covers the 10-28 on the race wheel for
  a flat race, the Flo Disk for a race day, and a 10-34 on the road
  bike's rear wheel for a week in the mountains. Dates rather than
  per-activity picks, because a swap lasts more than one ride (warm-up,
  race and cool-down, or a whole trip), and picking a cassette ride by
  ride for a week is a chore.
- **A part's distance** is `start_km` plus the distance of every ride on
  a bike it was on that day, directly or, for a cassette or tyre, on a
  wheel that was on the bike. The bike of a ride is the one spec 67
  settles: picked, or the default.
- **Errors**, as for the rest of `gear.json` (a message box the first
  time, the status line after): a part without a known `type`; a part
  on the wrong kind of place (a cassette on a bike or a front wheel, a
  chain or wheel on a wheel, a tyre on a bike, anything nested in a
  chainset, chain, cassette or tyre); a swap naming a part or place that
  isn't there, putting a part on the wrong kind of place, or with
  `until` before `from` or missing; two swaps of one part, or onto one
  place for one type, that overlap.
- **Indoor rides** (the `indoor` kind: trainer, Zwift) count for the
  chain and the chainset, not for the wheels, their cassette or tyres: on a
  direct-drive trainer they don't turn. The trainer's own cassette isn't
  tracked.

### Waxing

- **A chain's `waxed`** is a list of the days it was waxed, in any order
  (the app sorts them): `"waxed": ["2026-11-20", "2026-08-16"]`.
- **A wax counts from the next day on**, as chains are usually waxed
  after the day's ride. For a wax before a ride that day, the entry can
  have a time: `"2026-11-20T08:00"`, and rides that start after it
  count as after the wax.
- **The distance since the last wax** is that of the rides the chain
  did after it. A chain never waxed has none: it's just its distance.
- `waxed` on anything but a chain, or an entry that isn't a date or a
  date and time, is an error.

### Wear limits

- **Any piece of gear can have a `max_km`**, shoes and bikes too, and a
  chain a `wax_km`: `"max_km": 3000, "wax_km": 300`. Both optional.
- Over its limit, the distance (or the distance since waxed) is shown in
  orange on the Gear screen: time to replace the part, or wax the
  chain. That's what the feature is for, and it's cheap.

### The Gear screen

- **Parts are a third group**, after shoes and bikes, in the order of
  the file (bike by bike, each wheel followed by its parts, then
  `unfitted`), retired parts dimmed at the bottom, as for the rest.
- **Two new columns**, blank for the rows they don't apply to:
  - **On**: where the part is today, swaps included: `Roadie`, or
    `Flo45 rear` for a cassette or tyre; `–` for a part in `unfitted`
    (or on a wheel in `unfitted`) and for a retired part.
  - **Waxed**: for a chain, the distance since the last wax and its
    day, `212 km (20 Nov)`; `–` for a chain never waxed.
- A flat group with an On column rather than a tree under each bike:
  the file's order already reads bike by bike, and a swap puts a part
  under another bike for a few days.
- The distances are updated as the bikes' are: from the summaries, and
  the sidecars' picks of bikes.

### The activity info box

Unchanged: it shows the bike, not its parts. Six more lines a ride is
a lot, and the Gear screen is where wear is checked. Showing only the
parts of a temporary swap, such as `Cassette: 10-28` on a race, is
*(deferred)*.

### Version 1 scope

- Parts nested in `fitted` and `unfitted`, `since`, `swaps`, waxing,
  wear limits, and the parts on the Gear screen.
- Picking parts per activity in the app, with sidecar keys, as the bike
  is picked, is *(deferred)*. Jan asked for different wheels and
  chainsets per activity, and swaps cassettes more often than
  chainsets: a 10-28 on the race wheels (usually a 10-33) for a flat
  race, a 10-33 or 10-36 on the road bike (usually a 10-28) for the
  mountains. `swaps` covers those, and pickers can come if editing the
  file becomes a chore.
- The README's `gear.json` section gets `fitted`, `unfitted`, `since`,
  `swaps`, `waxed`, `max_km` and `wax_km`.
- **What carries over from the branch** (the dated-list version): front
  and rear wheels, `max_km`/`wax_km`, waxing by date or time, the
  totals, `Table`'s orange cells, and the Gear screen's On and Waxed
  columns. What goes: the top-level `parts`, `fitted` and `waxed` lists.
  `until` survives in `swaps`.

## Open questions

None.

## Architecture

> **Out of date**: this section describes the dated-list version built
> on the branch. The Developer revises it for the nested `gear.json`;
> much of it (`PARTTYPES`, `fits_on()`, waxes, totals, `Table`, the
> Gear screen) carries over. The Test plan likewise.

### Parts, fittings and waxes (`storage/Gear.h`, `.cpp`)

Parts are gear, so they go in the same `GearList`, and everything that
decides where a part is and what it did stays pure functions, as in
spec 67.

- `GEARTYPES` gets `Part`, `"parts"`, after `Bike`. `GearList::gear`
  keeps shoes, bikes, then parts, each in the file's order, and `find()`
  covers them all, so a name stays unique over the three.
- `PARTTYPES`, an X-macro: `Chainset` `"chainset"`, `FrontWheel`
  `"front wheel"`, `RearWheel` `"rear wheel"`, `Cassette` `"cassette"`,
  `Chain` `"chain"`, `Tyre` `"tyre"`, with `PartType_name()`,
  `PartType_by_name()`, and `fits_on(PartType, Gear const &place)`: a
  chainset, chain or wheel on a bike, a cassette on a rear wheel, a
  tyre on either wheel.
- `Gear` gets `std::optional<PartType> part` (set for parts only),
  `std::optional<float64> max_km`, for any gear, and
  `std::optional<float64> wax_km`, for a chain.
- `struct Fitting { std::string part; std::string on; year_month_day from; std::optional<year_month_day> until; }`
  with `in_effect(day)`: `from <= day <= until`.
- `struct Wax { std::string chain; year_month_day day; std::chrono::local_seconds counts_from; }`.
  `counts_from` is midnight after `day` for `"2026-11-20"`, the time
  itself for `"2026-11-20T08:00"`; a ride counts after the wax if its
  start, in local time, is at or after it. `day` is for the screen.
- `GearList` gets `std::vector<Fitting> fitted` and
  `std::vector<Wax> waxed`, each sorted by date, the file's order among
  those of one day (as `defaults`).
- `decode_gear()` reads `parts` (as a bike, plus `type`), `fitted`,
  `waxed`, `max_km` and `wax_km`, and accepts the three new top-level
  keys. New errors: a part without a known `type`; a `fitted` entry
  naming a part that isn't a part, or a place that isn't there, or the
  wrong kind of place; `until` before `from`; a `waxed` entry naming anything but a
  chain, or an `on` that isn't a date or a date and time; a `max_km` or
  `wax_km` that isn't a number over 0, or a `wax_km` on anything but a
  chain. A default naming a part is an error too ("isn't in `shoes` or
  `bikes`").
- `fitting_of(list, part, day) -> Fitting const *`: the part's latest
  entry in effect on `day`, none if it isn't fitted, or if a later part
  has taken its place there (the 11-28 during the 11-34's week).
- `part_at(list, place, type, day) -> Gear const *`: the part in a
  place: of the entries in effect there for that type, latest first, the first whose part's `fitting_of()` is that entry.
  This is the spec's "a place has the part of the latest entry in
  effect there whose part is still there".
- `parts_used(list, bike, day, indoor) -> std::vector<Gear const *>`: the
  parts a ride on `bike` wears that day: the bike's chainset, chain and
  wheels, the front wheel's tyre, and the rear wheel's cassette and
  tyre; only chainset and chain when `indoor`.
- **A retired part** isn't counted for rides after its `retired` day,
  and isn't "on" anything after it, as a default's gear stops counting
  once it's retired. The part it replaced doesn't come back: retiring a
  chain without fitting a new one leaves the bike without a chain in
  the numbers, until there's a `fitted` entry. *(A decision of mine;
  the spec doesn't say.)*
- A sidecar pick naming a part (only by hand) is shown as unknown, and
  the picker never offers parts.
- `GearTotal` gets `std::string on` (where the part is today, empty for
  shoes and bikes) and `std::optional<WaxTotal> wax` for a chain
  (`std::optional<year_month_day> last`, `float64 km` since it), and
  `worn()` (over `max_km`) and `needs_wax()` (over `wax_km` since the
  last wax, or in all for a chain never waxed), both false for retired
  gear. `gear_totals()` takes `today`, for the On column
  and the wax: for each ride it finds the bike as before, then adds the
  ride to each of `parts_used()`, and to a chain's wax total if it
  started after the chain's last wax. Parts come after bikes, retired
  last, as the rest.
- The ride's local start, for the wax, is
  `local_start(summary) -> local_seconds`, in `storage/Volume` next to
  `start_day()`, from the same `DateTime::from_timestamp()`.
- Cost: each ride looks through `fitted` a few times; with a few dozen
  entries and 6,000 rides that's well under the sidecar read the screen
  already waits on, so no index.

### The widget library

- `Table` gets cells drawn in orange, for a value that needs attention:
  `std::set<std::pair<size_t, size_t>> highlighted` (row, column),
  passed to `set_rows()` as a fourth argument, and
  `is_highlighted(row, column)`. It wins over dimmed and selected.
  Per cell rather than per row: only the distance, or the waxed
  column, is over its limit.

### The Gear screen (`widget/Gear.h`, `.cpp`)

- Columns: Gear, Brand and model, In use since, **On**, Activities,
  Distance, **Waxed**. On is `Canyon`, `Race rear`, or `–`;
  Waxed is `212 km (20 Nov)`, or `–` for a chain never waxed; both blank
  for the rest.
- `GearColumn` names the columns' indexes.
- `gear_highlights(totals)` gives the table's orange cells: Distance
  for `worn()`, Waxed for `needs_wax()`, or Distance for a chain never
  waxed, which has no Waxed to show it on.
- The note counts parts too, when there are any:
  `2 pairs of shoes, 2 bikes, 7 parts`.
- `GearScreen::open()` takes today, the computer's local day, as
  `SummariesScreen::open()` does.
- Until the picks are read, Waxed is blank with the other numbers.

### README

The `gear.json` section gets `parts`, `fitted` (with `until`), `waxed`, `max_km` and `wax_km`, with the spec's example.

No new dependencies, files or widgets beyond the above.

## Test plan

- `gear_parts_test` (new, as `gear_test` is long enough):
  - `decode_gear()`: the spec's example with parts, `fitted` and
    `waxed`; each new error: unknown part type, fitted part unknown or
    not a part, unknown place, a cassette on a bike or a front wheel, a
    chain on a wheel, a wheel on a wheel, a tyre on a bike, `until`
    before `from`, a wax of
    a cassette, a bad wax date, `wax_km` on a bike, a default naming a
    part; the new top-level keys accepted.
  - `fitting_of()` / `part_at()`: the latest entry wins; `until` ends a
    swap and the part before is back (inclusive of the `until` day);
    a wheel moved to another bike leaves the first with the wheel it
    had before; both moved on leaves it with none; two entries on one day,
    the later in the file wins.
  - `parts_used()`: a road ride wears chainset, chain, both wheels,
    cassette, both tyres; a tyre moved from the rear wheel to the front
    keeps its distance; an indoor ride only chainset and chain; the swap
    cassette during its `until` window.
  - `gear_totals()`: part distances from rides on the bike (picked and
    default), `start_km` added, indoor rides not on the wheels, a
    retired part not counted after its day; the On text; wax since a
    date (not counting that day's ride) and since a time (counting a
    later ride that day); a chain never waxed; `worn()`/`needs_wax()`,
    and not for retired gear.
  - `gear_choices()`: parts are never offered.
- `gear_screen_test`: the On and Waxed cells, `–`s, blanks for shoes and
  bikes, the orange cells, the note with parts.
- `table_test`: `highlighted` follows `set_rows()`.
- By hand: the spec's example in `gear.json`, the Gear screen's parts,
  an orange distance after lowering a `max_km`, and a broken `fitted`
  entry's message box.

## Review notes

1. **Bug.** A sidecar pick naming a part is credited to the part.
   `activity_gear()` marks it `unknown`, as the spec says, but
   `gear_totals()` (`src/App/storage/Gear.cpp`, the `add(used->name, summary)`
   call) adds the ride to `by_name[part]`, which is the part's own total:
   a pick of `KMC blue` gives the chain the ride's km and an activity,
   and no `Unknown:` row shows, as `GearTotal::unknown()` is false for a
   name that is in the list. Only by hand-editing a sidecar, so low
   severity. The fix is to credit the ride only when `!used->unknown`, and
   to show an unknown row for it otherwise. Test
   `test_pick_of_a_part` in `test/GearPartsTest.cpp` fails until then.
2. **Suggestion.** `Fitting` entries of a part are checked against its
   place only when read, so a part retired or a place later in the file is
   fine, but a `fitted` entry before the part's own `parts` entry can't
   occur (parts are decoded first); no change needed. Everything else
   checked held up: `part_at()`/`fitting_of()` follow the spec's rule
   (including wheels moving, both moving on, the same-day later-wins and
   `until` inclusive), wax counts from the next day or the time in local
   time, indoor rides skip wheels, cassette and tyres, and the build and
   all 90 tests passed before my test was added.

### Second review (front and rear wheels)

Reviewed ef2dd75, 3425897 and c3f91c1. The placement rules, `fits_on()`,
`check_fitting()`, `part_at()` and `parts_used()` are correct, and no
`wheels`, `at` or `TyrePosition` is left in the code, tests, README or
spec (the remaining "wheels" are plain English).

1. **Missing test (added).** `test_parts_follow_their_wheel` in
   `test/GearPartsTest.cpp`: a rear wheel moved to another bike takes its
   cassette with it and leaves the first bike its front wheel and tyre;
   a tyre on a wheel that's on no bike is still fitted on the wheel
   (`fitting_of()`), but counts for no ride. It passes.
2. **Suggestion.** A `gear.json` from before this change fails to load
   with a `type` error naming the new types, which is right, but an
   old `"at"` on a `fitted` entry is silently ignored. Harmless once the
   wheels are split, so no change needed.


### Resolution

1. Fixed in 2caf037, as Jan chose: a pick naming a part gets an
   `Unknown: KMC blue` row, as any name not in `gear.json` does, and its
   ride counts for no part. The unknown names are kept apart from the
   gear in `gear_totals()`. `test_pick_of_a_part` also checks the row.
2. Nothing to change.
