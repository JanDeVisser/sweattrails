# Bike components

Status: In review, on branch feature/68-bike-components

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

- **Parts are gear too**, in a new `parts` list in `gear.json`, with the
  same fields as a bike (name, brand, model, purchased, where, price,
  `start_km`, `retired`) and a `type`: `chainset`, `wheels`, `cassette`,
  `chain` or `tyre`. Tyres were added to what spec 67 listed: they wear
  out as the rest do. The name is unique over shoes, bikes and parts.
- **Where a part is fitted is a dated list**, `fitted`, like `defaults`:

  ```json
  "fitted": [
    { "part": "Ultegra 52/36", "on": "Canyon", "from": "2022-05-01" },
    { "part": "Canyon stock wheels", "on": "Canyon", "from": "2022-05-01" },
    { "part": "105 11-28", "on": "Canyon stock wheels", "from": "2022-05-01" },
    { "part": "GP5000 front", "on": "Canyon stock wheels", "at": "front", "from": "2026-03-01" },
    { "part": "GP5000 rear", "on": "Canyon stock wheels", "at": "rear", "from": "2026-03-01" },
    { "part": "KMC blue", "on": "Canyon", "from": "2026-06-01" },
    { "part": "105 11-34", "on": "Canyon stock wheels", "from": "2026-07-01", "until": "2026-07-12" }
  ]
  ```

  A chainset, wheels or chain is fitted on a bike; a cassette or a tyre
  on wheels. A tyre says where, `"at": "front"` or `"rear"`, as the rear
  wears faster and is replaced on its own.
- **From its date on, a part is where its entry puts it**, taking the
  place of the part of the same type (and, for a tyre, position) that
  was there, until a later entry. `until` is optional: after that day
  the entry no longer applies, and what was there before it is back.
  That's a temporary swap in one line: a 10-28 on the race wheels for a
  flat race, or a 10-34 on the road bike's wheels for a week in the
  mountains. Dates rather than per-activity picks, because a swap lasts
  more than one ride (warm-up, race and cool-down, or a whole trip).
- **A part is in one place at a time**: on a day, it's where its latest
  entry in effect puts it, and a place has the part of the latest entry
  in effect there whose part is still there. So moving wheels to
  another bike takes them off the first, which then has the wheels it
  had before them, if those haven't moved on as well.
- **Why a dated list, and not parts listed under each bike**: a part
  that moves between bikes, such as a pair of wheels, keeps its whole
  history in one list, and the rule is the same "from this day on" as
  `defaults`. Swaps are rare (Jan doesn't rotate chains), so editing
  `fitted` by hand is fine; an in-app "swap chain" isn't needed.
- **A part's distance** is `start_km` plus the distance of every ride on
  a bike it was on that day, directly or, for a cassette or tyre, on
  wheels that were on the bike. The bike of a ride is the one spec 67
  settles: picked, or the default.
- **Errors**, as for the rest of `gear.json` (a message box the first
  time, the status line after): a `fitted` entry naming a part or place
  that isn't there, a part on the wrong kind of place (a cassette on a
  bike, a chain on wheels), a tyre without `at`, or `until` before
  `from`.
- **Indoor rides** (the `indoor` kind: trainer, Zwift) count for the
  chain and the chainset, not for the wheels, their cassette or tyres: on a
  direct-drive trainer they don't turn. The trainer's own cassette isn't
  tracked.

### Waxing

- **A `waxed` list** in `gear.json`, an entry per wax:

  ```json
  "waxed": [ { "chain": "KMC blue", "on": "2026-11-20" } ]
  ```

- **A wax counts from the next day on**, as chains are usually waxed
  after the day's ride. For a wax before a ride that day, the entry can
  have a time: `"on": "2026-11-20T08:00"`, and rides that start after
  it count as after the wax.
- **The distance since the last wax** is that of the rides the chain
  did after it. A chain never waxed has none: it's just its distance.
- A `waxed` entry naming a part that isn't a chain is an error.

### Wear limits

- **Any piece of gear can have a `max_km`**, shoes and bikes too, and a
  chain a `wax_km`: `"max_km": 3000, "wax_km": 300`. Both optional.
- Over its limit, the distance (or the distance since waxed) is shown in
  orange on the Gear screen: time to replace the part, or wax the
  chain. That's what the feature is for, and it's cheap.

### The Gear screen

- **Parts are a third group**, after shoes and bikes, in the order of
  the file, retired parts dimmed at the bottom, as for the rest.
- **Two new columns**, blank for the rows they don't apply to:
  - **On**: where the part is today, `Canyon`, `Race wheels`, or
    `Race wheels, rear` for a tyre; `–` for a part that isn't fitted.
  - **Waxed**: for a chain, the distance since the last wax and its
    day, `212 km (20 Nov)`; `–` for a chain never waxed.
- A flat group rather than parts listed under their bike: a part
  that's off every bike, or has moved between bikes, has no single
  place under a bike.
- The distances are updated as the bikes' are: from the summaries, and
  the sidecars' picks of bikes.

### The activity info box

Unchanged: it shows the bike, not its parts. Six more lines a ride is
a lot, and the Gear screen is where wear is checked. Showing only the
parts of a temporary swap, such as `Cassette: 10-28` on a race, is
*(deferred)*.

### Version 1 scope

- Parts, `fitted` with `until`, waxing, wear limits, and the parts on
  the Gear screen.
- Picking parts per activity in the app, with sidecar keys, as the bike
  is picked, is *(deferred)*. Jan asked for different wheels and
  chainsets per activity, and swaps cassettes more often than
  chainsets: a 10-28 on the race wheels (usually a 10-33) for a flat
  race, a 10-33 or 10-36 on the road bike (usually a 10-28) for the
  mountains. `fitted` with `until` covers those, and pickers can come if
  editing the file becomes a chore.
- The README's `gear.json` section gets parts, `fitted`, `waxed`,
  `max_km` and `wax_km`.

## Open questions

None.

## Architecture

### Parts, fittings and waxes (`storage/Gear.h`, `.cpp`)

Parts are gear, so they go in the same `GearList`, and everything that
decides where a part is and what it did stays pure functions, as in
spec 67.

- `GEARTYPES` gets `Part`, `"parts"`, after `Bike`. `GearList::gear`
  keeps shoes, bikes, then parts, each in the file's order, and `find()`
  covers them all, so a name stays unique over the three.
- `PARTTYPES`, an X-macro: `Chainset` `"chainset"`, `Wheels` `"wheels"`,
  `Cassette` `"cassette"`, `Chain` `"chain"`, `Tyre` `"tyre"`, with
  `PartType_name()`, `PartType_by_name()`, and
  `part_goes_on(PartType) -> GearType` (`Bike`, or `Part` for wheels).
  `TYREPOSITIONS`: `Front` `"front"`, `Rear` `"rear"`.
- `Gear` gets `std::optional<PartType> part` (set for parts only),
  `std::optional<float64> max_km`, for any gear, and
  `std::optional<float64> wax_km`, for a chain.
- `struct Fitting { std::string part; std::string on; std::optional<TyrePosition> at; year_month_day from; std::optional<year_month_day> until; }`
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
  wrong kind of place; a tyre without `at`, or `at` on anything but a
  tyre; `until` before `from`; a `waxed` entry naming anything but a
  chain, or an `on` that isn't a date or a date and time; a `max_km` or
  `wax_km` that isn't a number over 0, or a `wax_km` on anything but a
  chain. A default naming a part is an error too ("isn't in `shoes` or
  `bikes`").
- `fitting_of(list, part, day) -> Fitting const *`: the part's latest
  entry in effect on `day`, none if it isn't fitted, or if a later part
  has taken its place there (the 11-28 during the 11-34's week).
- `part_at(list, place, type, at, day) -> Gear const *`: the part in a
  place: of the entries in effect there for that type (and position),
  latest first, the first whose part's `fitting_of()` is that entry.
  This is the spec's "a place has the part of the latest entry in
  effect there whose part is still there".
- `parts_used(list, bike, day, indoor) -> std::vector<Gear const *>`: the
  parts a ride on `bike` wears that day: the bike's chainset, chain and
  wheels, and the wheels' cassette and tyres; only chainset and chain
  when `indoor`.
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
  Distance, **Waxed**. On is `Canyon`, `Race wheels, rear`, or `–`;
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

The `gear.json` section gets `parts`, `fitted` (with `until` and
`at`), `waxed`, `max_km` and `wax_km`, with the spec's example.

No new dependencies, files or widgets beyond the above.

## Test plan

- `gear_parts_test` (new, as `gear_test` is long enough):
  - `decode_gear()`: the spec's example with parts, `fitted` and
    `waxed`; each new error: unknown part type, fitted part unknown or
    not a part, unknown place, a cassette on a bike, a chain on wheels,
    a tyre without `at`, `at` on a chain, `until` before `from`, a wax of
    a cassette, a bad wax date, `wax_km` on a bike, a default naming a
    part; the new top-level keys accepted.
  - `fitting_of()` / `part_at()`: the latest entry wins; `until` ends a
    swap and the part before is back (inclusive of the `until` day);
    wheels moved to another bike leave the first with the wheels it had
    before; both moved on leaves it with none; two entries on one day,
    the later in the file wins.
  - `parts_used()`: a road ride wears chainset, chain, wheels, cassette,
    both tyres; an indoor ride only chainset and chain; the swap
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

