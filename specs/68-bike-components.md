# Bike components

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

*(Revised 2026-11-24 for the nested `gear.json`. What carries over from
the dated-list version is unchanged: `PARTTYPES`, `fits_on()`, `max_km`
and `wax_km`, parsing a wax's date or time, `parts_used()`, `worn()` and
`needs_wax()`, `Table`'s orange cells, and the Gear screen's columns.)*

### Parts, places and swaps (`storage/Gear.h`, `.cpp`)

The tree is read into the same flat `GearList` as before, each part
knowing the name of the place it's listed in. Everything that decides
where a part is on a day stays a pure function of the list and the day.

- `GEARTYPES` keeps `Part`, though there's no top-level `parts` any
  more. `GearList::gear` keeps shoes, then bikes, then parts. Parts are in the
  file's order, depth first: bike by bike, a wheel followed by its
  parts, then `unfitted`. That is the order of the Gear screen.
- `PARTTYPES`, `PartType_name()`, `PartType_by_name()` and
  `fits_on(PartType, Gear const &place)` are unchanged.
- `Gear` loses nothing and gets, for parts only:
  - `std::string place`: the name of the bike or wheel it's listed in;
    empty for a part at the top of `unfitted`.
  - `std::optional<year_month_day> since`, as written, and
    `fitted_since() -> std::optional<year_month_day>`: `since`, else
    `purchased`, else none (it's always been there).
  - `std::vector<Wax> waxed`, for a chain, sorted by when they count
    from. `Wax` loses its `chain`: `{ day, counts_from }`.
- `struct Swap { std::string part; std::string on; year_month_day from; year_month_day until; }`
  with `in_effect(day)`, both ends inclusive. `GearList` gets
  `std::vector<Swap> swaps`, in the file's order, and loses `fitted` and
  `waxed`. `Fitting` goes.
- `decode_gear()`:
  - Top-level keys: `shoes`, `bikes`, `unfitted`, `defaults`, `swaps`.
    An old file's `parts`, `fitted` or `waxed` is "not one of" these, so
    it fails loudly rather than losing its parts.
  - Shoes, then bikes, are read as now. Then each bike's `fitted`, then
    `unfitted`, recursively, a part read as a bike plus `type`, `since`
    and `waxed`, and its own `fitted` if it's a wheel.
  - Where an entry is, in errors: `bikes 2`, `unfitted 1`, and for a
    nested one, by its place's name: ``fitted 3 on `Roadie` ``. Errors
    about a part with a name use its name: ``` `SRAM AXS 10-30`, a
    cassette, goes on a rear wheel, and `Roadie` isn't one ```.
  - New errors: `fitted` on shoes, or on a chainset, chain, cassette or
    tyre (``` `KMC` has `fitted`, but only bikes and wheels do ```); a part on the wrong kind of place; `waxed` that isn't a
    list, on anything but a chain, or with an entry that isn't a date or
    a date and time; a swap whose `part` isn't a part, whose `on` isn't
    a bike or a wheel, which puts the part on the wrong kind of place,
    which has no `from` or `until`, or `until` before `from`; two swaps
    of one part that overlap; two swaps that overlap putting parts of
    one type on one place. The rest is as before (unknown part `type`,
    `max_km`, `wax_km` on a non-chain, names unique over everything,
    wherever nested; a default naming a part).
  - `since` and `type` on shoes or bikes are ignored, as any unknown
    field in an entry is (for notes); `fitted` on shoes isn't, as it's
    surely a mistake, nor is `waxed`, which the spec says is an error on
    anything but a chain. *(A decision of mine; the spec doesn't
    say.)*
- Where a part is, on a day:
  - `resident_at(list, place, type, day) -> Gear const *` (in the
    `.cpp`): of the parts of `type` listed in `place` whose
    `fitted_since()` is none or on or before `day`, the one with the
    latest; on a tie, the later in the file. None if it was retired
    before `day`.
  - `part_at(list, place, type, day) -> Gear const *`: the part of a
    swap in effect onto `place` for `type`, if there is one; else the
    resident, unless the resident is swapped onto somewhere else that
    day. None for a retired part.
  - `place_of(list, part, day) -> Gear const *`, replacing
    `fitting_of()`: the bike or wheel the part is on, a swap's place if
    one is in effect, else its own place if `part_at()` there is the
    part. None if it's on nothing, or retired.
  - A part nested in a wheel follows the wheel with no extra work:
    `parts_used()` (unchanged) asks for the bike's rear wheel, then that
    wheel's cassette, so a swapped-in Flo Disk brings its 10-28.
- `gear_totals()`: `GearTotal::fitted` becomes `std::string on`, where a
  part is today: its `place_of()`, if that's a bike, or a wheel that's
  on a bike today; empty otherwise (shown as `–`). So a tyre on a wheel
  in `unfitted` is on `–`, as the spec says. A chain's waxes come from
  its own `waxed`. The rest is unchanged.
- Cost: `resident_at()` looks through the parts, `part_at()` through the
  swaps; a few dozen of each, seven parts per ride, 6,000 rides: well
  under the sidecar read the screen already waits for.

### The widget library

Nothing new: `Table`'s orange cells are on the branch already.

### The Gear screen (`widget/Gear.h`, `.cpp`)

Unchanged but for `on_text()`, which reads `GearTotal::on`.

### README

The `gear.json` section's "A bike's parts" is rewritten for `fitted`,
`unfitted`, `since`, `retired` keeping the history, `swaps`, `waxed`,
`max_km` and `wax_km`, with the spec's example.

No new files, dependencies or widgets.

## Test plan

- `gear_parts_test`, rewritten for the nested file:
  - `decode_gear()`: the spec's example (parts in the file's order,
    their places, `since`, waxes, the swap); each new error: a part
    without a known `type`, each wrong place (a cassette on a bike or a
    front wheel, a chain or a wheel on a wheel, a tyre on a bike),
    `fitted` in a chainset, chain, cassette or tyre and on shoes, a part
    named twice in two trees, `waxed` on a cassette or with a bad entry,
    each swap error, both kinds of overlap (and a swap ending the day
    before another starts is fine); `parts`, `fitted` and `waxed` at
    the top are errors.
  - `part_at()` / `place_of()`: the latest `since` wins, `purchased`
    standing in for it, a part without either always there; on the day
    of a replacement the new part; a retired part leaves its place
    empty; a swap from `from` to `until` inclusive, its part's own place
    empty meanwhile, and the swapped-out part off; a swapped-in wheel
    brings its cassette and tyre.
  - `parts_used()`: a road ride, an indoor ride, during a swap.
  - `gear_totals()`: the distances (start_km, indoor, retired, a
    replaced chain's history, a swap's days), the On text (a bike, a
    wheel, `–` for `unfitted` and a tyre on an unfitted wheel, a swap's
    place today), waxes by date and by time, a chain never waxed.
  - Carried over: limits, the picker leaving out parts, a pick of a
    part shown as unknown.
- `gear_screen_test`: the On cell from `on`; the rest unchanged.
- By hand: Jan's `gear.json` on the Gear screen, a swap, and a broken
  part's message box.

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

### Third review (nested parts)

Reviewed b744087, d1dbd30, b17629e and fb702e0. `resident_at()`,
`part_at()` and `place_of()` follow the spec's rules: the latest `since`
wins, `purchased` stands in, a tie goes to the later in the file, a part
retired before the day leaves the place empty, a swap is inclusive at
both ends and leaves its part's own place without it, and a swapped
wheel brings its cassette and tyre, leaving its bike's wheel place to the
swapped-in one. Every decode error the spec lists is there, with the
overlap rules (one part twice, one type on one place) and back-to-back
swaps allowed. The On text is blank for a part on a wheel in `unfitted`,
and the README matches the spec. The build and all 90 tests pass.

1. **Missing test (added).** `test_swapped_between_bikes` in
   `test/GearPartsTest.cpp`: a rear wheel swapped from one bike to
   another takes its cassette along and leaves its own bike without a
   wheel; a cassette swapped onto a swapped-in wheel replaces the wheel's
   own; a chain swapped to another bike counts indoors there and not on
   its own bike; a retired part swapped in is nothing; the On text on the
   swap's day (the wheel on the other bike, its cassette on the wheel, the
   wheel it left and its tyre `–`). It passes.
2. **Suggestion.** A part with `retired` equal to today still shows its
   bike in On (`retired_on()` is `retired < day`, so it still counts for
   today's rides), where the spec says `–` for a retired part. It's
   consistent with the distances, and gone tomorrow, so no change
   needed; say so in the spec if that's the intent.
3. **Suggestion.** `place_of()` for a part on a wheel that is itself off
   the bike returns the wheel, as the architecture says; only
   `gear_totals()`'s `on_a_bike()` turns that into `–`. That's right, but
   worth knowing when `place_of()` is used elsewhere.
4. **Suggestion.** A swap without a `part` or `on` says
   ``swap 1's `part`, ``, isn't a part...`` with empty backticks; a
   message naming the missing field would read better. Cosmetic.
