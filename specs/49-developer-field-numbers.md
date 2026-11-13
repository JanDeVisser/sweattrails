# Developer fields don't stand in for missing native fields

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

The maps of activities recorded by a Wahoo ELEMNT RIVAL are badly broken:
dozens of straight east-west lines run from the route off the right edge
of the map. An example is `activity/2025/03/1742214566.fit`, a run in
Waterloo. After this fix those maps show the route as it was recorded. 444
of the 5394 FIT files in Jan's store are affected.

## Feature

### What's wrong

The FIT file is fine. Every record either has both `position_lat` and
`position_long` or has neither. 209 of the example's 9187 records have no
position, scattered through the run in ones and twos. Those records use a
second record definition without the position fields.

SweatTrails reads those 209 records with a latitude of around 43.44° and a
longitude of exactly 0°. That's a point on the Greenwich meridian, so every
one of them draws a line east off the map and back.

The cause is a known TODO in `FITDataRecord::get_field()`
(`src/App/fit/FIT.cpp`). When a record has no native field with the number
asked for, it returns a *developer* field with that number. Developer field
numbers are a separate namespace. The Wahoo file declares 21 developer
data indices, and the field in each one is numbered 0. So a record without
native field 0 (`position_lat`) gets developer field 0 (index 0, a sint32
with units "adc") as its latitude. There's no developer field 1, so
`position_long` isn't found and stays at `Coordinates`' default of 0. Since
`has_fix()` only asks that one of the two is away from 0, the point counts
as a fix.

The same mix-up can hit any native field a record lacks, from any device
that writes developer fields, e.g. a Stryd on a Garmin. So it's fixed at
the source, not in the map.

### What changes

1. **Native field lookups return only native fields.** A record that
   doesn't have a native field doesn't have it, whatever developer fields
   it carries. The compressed timestamp still counts as the native
   timestamp. Nothing in the app reads developer fields by number today.
   If something needs them later, that's a separate lookup by developer
   data index and field number *(deferred)*.
2. **A position needs both coordinates.** A record that has only a
   latitude or only a longitude has no position, as if it had neither.
   After point 1 this doesn't happen with the files we know of. It guards
   against a device that writes one without the other, since half a
   position always draws as a line to the equator or to Greenwich.
3. **fitexplore prints coordinates with the right sign.** Its
   `format_field_value()` divides by `1 << 31`, an `int` that overflows
   to −2³¹, so it prints Waterloo as `-43.4º lat, 80.6º long`. The loader
   itself (`assign_value()`) uses `1u << 31` and is right. This was found
   while tracking down the bug, and the fix is one character per line.

Records without a position are already handled: the activity map draws
them where the record before them was (`ActivityMap.cpp`), gap finding
skips them (`Gaps.cpp`), and the overlay's track only takes records with
a position.

### Nothing to migrate

No positions are cached. The summary files (`27-volume-summaries.md`)
hold no positions or boxes, the activity's box is worked out at load time,
and the box already ignored these points because it wants both
coordinates more than 1° from 0. So the fix shows on the next load, and
no files need rewriting.

### Version 1 scope

Points 1 to 3. Deferred:

- Looking up developer fields by index and number, and showing them by
  name in fitexplore.

Gap filling (`30-fill-gaps.md`) looks for gaps in the recorded positions,
and these half-positions are kilometres from their neighbours, so it could
have found gaps in Wahoo activities that weren't there. It didn't happen:
the store's only filled activities (with a `<start>.fit.original`,
2026/10/1793448994 and 2026/11/1793967219) are Garmin recordings, and
they have no half-positions.

## Open questions

## Architecture

All three points are small changes to existing code; there are no new
types outside the tests.

1. `FITDataRecord::get_field()` (`src/App/fit/FIT.cpp`) loses its loop over
   the developer fields, and the TODO with it. It looks at the native
   fields, then at the compressed timestamp. That also settles the TODO's
   other case: a developer field numbered 253 no longer hides a compressed
   timestamp. A lookup of developer fields by index and number is left for
   when something needs it *(deferred)*.
2. The record's position is assembled field by field: `assign_optional()`
   fills `lat` from field 0 and `lon` from field 1 into the same
   `std::optional<Coordinates>`, so a record with one of them gets a
   position with the other at 0. `on_load<record>` (`src/App/fit/Profile.cpp`),
   which already finishes the record off from the raw `FITDataRecord`,
   resets `position` unless both fields are there with a value. This is in
   the loader rather than in `Coordinates::has_fix()`: a half position is
   wrong data, not a missing fix, and this way no consumer ever sees one.
   `has_fix()` stays as it is.
3. `format_field_value()` (`src/App/fit/FIT.h`) uses `1u << 31`, as
   `assign_value()` does.

## Test plan

A new test, `fit_test` (`test/FITTest.cpp`), reads FIT files built with
`TestFit.h` through `FITFile` and `make_from_rec<record>`. `TestFit.h`
gets what it takes to write developer fields: a `definition()` that takes
developer field definitions (and sets the header's developer data bit),
and helpers for the `developer_data_id` and `field_description` messages
that have to come before them.

- A record without a position, with a developer field 0 (a sint32, as
  Wahoo's), has no position, and `get_field(0)` finds nothing. Checked to
  fail on the old `get_field()`.
- A record with a native position and a developer field 0 keeps its
  native latitude.
- A record with only `position_lat`, and one with only `position_long`,
  have no position. A record with both has it, with the right signs.
- `format_record<mesg_num::record>` prints a Waterloo position as
  ` 43.4º lat` and `-80.5º long`. Checked to fail with `1 << 31`.

A test that a developer field 253 doesn't hide a compressed timestamp
was dropped: the reader can't read a developer field numbered 128 or
more at all (it compares a signed `char` with the description's `u8`, and
fails with `UnknownDeveloperField`), so that case can't come up. That
bug is outside this spec.

Checked by hand: fitexplore on the example file prints 8978 positions,
9187 records less the 209 without one, all at 43.4º lat and −80.6º long.

Not tested automatically: how the map draws. The map already skips
records without a position; checking the example file
(`activity/2025/03/1742214566.fit`) in the app is for Jan.

## Review notes

*Filled in by the Reviewer.*
