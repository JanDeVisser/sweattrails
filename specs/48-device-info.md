# Device info: the recording device and its sensors

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

The user can see the "technical" side of an activity: which FIT file it
was read from, the device that recorded it, and the sensors that were
connected to that device (heart rate strap, power meter, speed and
cadence sensors, radar, ...). This helps answer questions like "was the
power meter on for this ride?", "which watch did I use?" or "where is
the file for this one?".

## Feature

### What there is now

- `Activity` (`storage/Activity.h`) keeps the `file_id` message's
  manufacturer, `product_name` (which few devices fill in), serial number
  and file number. It doesn't keep `file_id`'s `product`, the number of
  the device's model.
- `device_name(activity)` (`widget/DataFields.cpp`) makes a readable
  name from the manufacturer and `product_name`, such as "Wahoo ELEMNT
  BOLT" or just "Garmin". The line under the title uses it for an
  activity recorded more than once.
- FIT's `device_info` message (number 23), which lists the recording
  device and each sensor, isn't decoded: `FIT.h` names it, but there's
  no struct or field metadata for it. `fitexplore` shows its fields only
  by number.
- What a real file has, a Garmin Edge ride from 2026-11-13: eight
  `device_info` messages at the start, repeated at least twice later in
  the ride and at the end. One per device index: the recording device
  itself (index 0, the "creator"), a few of its own parts (barometer, GPS
  and so on, local sensors), and the ANT+/BLE sensors, each with a
  device type, manufacturer, product number, serial number, software
  version and battery status. Few have a product name as text.

### The box

- **A popup**, not a section of the activity screen: spec 40 freed up
  that room for the laps, and the panel's height is carefully worked
  out. It opens on the activity being shown with the palette command
  "Activity info" (`st-activity-info`) and a key, and closes with Esc.
  Without an activity on screen, the command does nothing.
- **The file**: its path in the store, such as
  `activity/2026/11/1794572226.fit`. This is the file the activity was
  read from, not the original download's name, which the store doesn't
  keep. A "Show in Finder" action reveals it (`open -R` on macOS,
  opening its folder elsewhere), as the OAuth login opens the browser.
- **The recording device**: its maker, as `device_name()` writes it, and
  its product name if the file has one. Otherwise its product number,
  in the same smaller font, after the name: "Garmin (product 4315)".
- **The sensors**: one entry each, with the maker, the type, and the
  product number in the smaller font, as for the recording device:
  "Garmin heart rate monitor (product 4606)". Under that, in the smaller
  font, its serial number, software version, and battery status at the
  end of the activity (ok, low, critical, ...). The battery is the most
  useful of these: it answers "why did the power drop out?". Product
  numbers aren't turned into names in this version (see Version 1
  scope).
  - A sensor's product name is only shown for a type SweatTrails doesn't
    know, in place of the type: a Wahoo ELEMNT ROAM names its sensors by
    their type ("Radar", "Heartrate", "Power"), which would say it twice.
    Without a maker, the type starts the line: "Shifting".
  - The file lists each device several times: at the start, during the
    activity and at the end. Each device is shown once, with what its
    last listing says.
  - The recording device's own parts, such as its barometer and GPS,
    aren't shown. Only the sensors connected to it are.
- **Multiple recordings** (spec 31): the box shows the recording on
  screen. Switching recordings (spec 32) switches what the box shows.
- **A file without sensors**, such as one from Zwift, a Strava export or
  an old file: the box shows what the file does have, and says "No
  sensors listed in this file". A missing value, such as a sensor's
  serial number or battery status, is left out rather than shown as
  empty. So is a product name that isn't text, such as the leftover
  bytes Zwift writes, and a product number of 0, which Zwift writes too:
  a Zwift ride shows as "Zwift". A battery status of "unknown" doesn't
  replace a known one from an earlier listing: a sensor that dropped
  out may say "unknown" at the end, and its "low" is what explains it.

### Version 1 scope

In: the popup with the file, the recording device and its sensors, and
"Show in Finder".

Deferred:
- *(deferred)* Names for Garmin's product numbers, from FIT's
  `garmin_product` table (about a thousand entries), so that a Garmin
  device shows as "Edge 1050" rather than "Garmin (product 4315)". Other
  makers don't publish theirs, so their devices would stay as in
  version 1.

## Open questions

None.

## Architecture

Four layers, each testable without a window.

### FIT: decoding `device_info` (`fit/Profile.h`, `fit/Profile.cpp`)

- `struct device_info`, with `make_from_rec` and `format_record`
  specializations like `lap`'s, so `fitexplore` names its fields too. It
  decodes what the box needs: `timestamp` (253), `device_index` (0),
  `device_type` (1), `manufacturer` (2), `serial_number` (3), `product`
  (4), `software_version` (5, scale 100), `battery_status` (11),
  `source_type` (25) and `product_name` (27). All optional but the index.
- X-macro enums with `tag()`s: `source_type` (ant, antplus, bluetooth,
  bluetooth_low_energy, wifi, local), `battery_status` (new, good, ok,
  low, critical, charging, unknown), `antplus_device_type` and
  `ble_device_type`. `device_type` stays a `u8`: what it means depends on
  `source_type`.
- `file_id`'s `product` is already decoded; `Activity` now keeps it.

### Storage: the activity's sensors (`storage/Devices.h`, `.cpp`, new)

- `struct Sensor`: `source`, `device_type`, `manufacturer`, `product`,
  `product_name`, `serial_number`, `software_version`, `battery`, all
  optional.
- `struct SensorList` collects `device_info` messages by device index:
  `add(device_info const &)` merges a listing into what earlier ones said,
  a later value replacing an earlier one, and a value a later listing
  leaves out staying as it was. `sensors()` gives them by index, leaving
  out the creator (index 0) and the device's own parts (`source_type`
  local). A listing without a `source_type`, as in older files and a
  Wahoo ELEMNT ROAM's, counts as a sensor only if it has a serial number
  of its own: the device's own parts have the creator's serial number, or
  none at all (the ROAM lists its barometer, GPS and so on by name only).
- `Activity` gets `std::optional<u16> product` (from `file_id`) and
  `std::vector<Sensor> sensors`, filled on a deep load only, as the
  records and laps are. Its `product_name`, when `file_id` has none,
  is the recording device's own `device_info` listing's on a deep load:
  a Wahoo ELEMNT ROAM names itself only there.

### The box's text (`widget/ActivityInfo.h`, `.cpp`, new)

- `maker_name(std::optional<manufacturer>)`, taken out of
  `device_name()`, which keeps working as it does. Sensors use it too.
  It stays in `widget/DataFields` with `device_name()`, so that
  DataFields doesn't depend on the popup's code.
- `sensor_type_name(Sensor const &)`: "heart rate monitor", "power
  meter", "speed and cadence sensor", "radar", ... from the ANT+ or BLE
  type, and "sensor" for one it doesn't know.
- `describe(Activity const &, fs::path const &store)` returns an
  `ActivityInfo`: the file's path relative to the store (`fit_path()`,
  so a recording's is in its `.recordings` folder; `fit_path()` moves
  from `storage/Gaps` to `storage/Activity` for it, and gives an empty
  path for an activity not from the store), which recording it is
  ("Recording 2 of 2", for one of several), the recording device's line, and per sensor its line and detail line. A line is
  `{ text, small }`: the part in the normal font and the part in the
  smaller one ("Garmin heart rate monitor" + "(product 4606)"; the
  detail line is all small: "Serial 3615618938 · Software 5.10 ·
  Battery ok"). Missing values are left out; no sensors gives the "No
  sensors listed in this file" line.

### The popup (`widget/ActivityInfoBox.h`, `.cpp`, new) and the app

- `ActivityInfoBox : Modal` draws an `ActivityInfo`, and takes:
  - Esc: closes.
  - F, or a click on the hint at the bottom ("F: Show in Finder · Esc
    closes"; "Open folder" off macOS): reveals the file.
  - Tab and Shift+Tab: switches recordings. A modal has the keys, so the
    app's Tab doesn't run while it's open; the box calls back into the
    app's recording switch and then describes the recording now shown.
  The box gets these as callbacks (`describe`, `switch_recording(step)`,
  `reveal`), so a test drives it with keys and no app behind it.
- Its rows, title and hint are plain functions (`info_rows()`,
  `info_title()`, `info_hint()`), tested without fonts. The title says
  which recording it shows ("Activity info · Recording 2 of 2"), and the
  hint lists Tab only for several recordings, and F only for a file.
  `draw()` and `resize()` only measure and draw those.
- `st-activity-info`, "Activity info", Cmd+I (Ctrl+I off macOS: Get
  Info's key), when on the activity screen. Without an activity it does
  nothing.
- `show_recording_next_to()` takes the displayed activity directly, so
  the box can call it while it's open (it now asks
  `activity_for_keys()`, which says no under a modal).
- Show in Finder: `run_and_wait()` and `open_browser()` move out of
  `OAuth.cpp` into a new `Desktop.h`/`.cpp` in `src/App`, with
  `reveal_command(path)` (`open -R <file>` on macOS, `xdg-open <folder>`
  elsewhere: a pure function, tested) and `reveal_file(path)`, which runs
  it. `Desktop.h` goes into `.clang-format`'s App group.

### Library widgets

Nothing generic enough yet: the box is lines of an activity's details. If
a second info popup comes along, the two-font line layout could move to
`src/Lib/Widget` as an `InfoBox`.

## Test plan

- `device_info_test` (new, FIT files built with `TestFit.h`): a file with
  a creator, a local part, an ANT+ and a BLE sensor, each listed at the
  start and again at the end with another battery status. The deep load
  has the two sensors, by index, with the last battery status, and a
  value only the first listing had. A file without `device_info` has no
  sensors; a shallow load has none either. A listing without
  `source_type` and with the creator's serial isn't a sensor.
- `activity_info_test` (new): `describe()` on activities made by hand:
  product name versus number for the recording device, sensor lines and
  detail lines, missing values left out, unknown types, the "No sensors"
  line, the path relative to the store, also for a recording, and which
  recording it is.
- `desktop_test` (new): `run_and_wait()`, its test moved from
  `oauth_test`, and `reveal_command()` for the platform as compiled.
- `activity_info_box_test` (new), with `ScopedTestApp`: the rows, title
  and hint; Esc closes, F calls reveal with the file (and does nothing
  without one), Tab and Shift+Tab call the switch with +1 and -1 and
  describe again, other keys aren't taken.
- `DataFieldsTest`'s `device_name()` tests keep passing after the split.
- By hand: the drawing, the layout in both fonts, the click on the hint,
  Finder opening on the file, and Tab in the box on an activity of two
  recordings; and the real Garmin ride from 2026-11-13 showing its
  sensors.

## Review notes

1. Suggestion: a last listing of battery status `unknown` replaces an
   earlier `ok`, `low` or `critical` (`SensorList::add`,
   `storage/Devices.cpp`). Sensors that report no battery at the end, or a
   hub that lists a strap as `unknown` when it has just dropped out, would
   hide the "low" the user wants to see. Suggested fix: don't let
   `unknown` replace a known status. Not changed: the spec says the last
   listing wins, so it's Jan's call.
   **Resolved**: Jan agreed; fixed in "Keep a sensor's known battery
   status under a later "unknown"", with a test, and the spec's Feature
   section says so.
2. Suggestion: `ActivityInfoBox::process_key()` calls `switch_fnc`, then
   `refresh()` and `resize()`, also for an activity of one recording,
   where `show_recording_next_to()` returns without doing anything. It's
   harmless (the box describes the same activity again), and the hint
   doesn't offer Tab then. No change needed.
   **Resolved**: left as it is, with Jan's agreement.
3. Suggestion: `hint_area` is only set by `draw()`, so it is stale for a
   frame after Tab changes the box's size. A click in that frame can miss
   or hit the hint wrongly. Not worth fixing.
   **Resolved**: left as it is, with Jan's agreement.
4. Missing test (added): `SensorList` without a creator listing, the
   creator never being a sensor whatever its source, and a later listing
   that says only some things keeping the rest (`device_info_test`). All
   pass.
5. Not checked, as there is no window: the drawing, the click on the hint,
   Finder opening, Tab with two recordings, as the spec's "by hand" list
   says. The real ride's `device_info` messages (via `fitexplore`) match
   what the tests build: index 0 and the parts are `local`, the strap
   (index 3) is ANT+ with type 120, and would show as "Garmin heart rate
   monitor (product 4606)". The spec's example says product 3865, which
   is index 2, a local part: the spec text is off, the code is right.
   **Resolved**: the spec's examples now say product 4606.

Found by Jan, checking by hand:

6. Bug: a Zwift ride showed "Zwift ?>?". Zwift fills its `file_id`'s
   product name with leftover bytes (`14 3e d6 9c 00 ...`), which were
   taken as the device's name; `device_name()` has shown them under the
   title for a Zwift recording among several since spec 31. **Fixed** in
   "Leave out product names that aren't text, and product number 0":
   `product_text()` drops a name that isn't UTF-8 or has control
   characters, and `product_number()` drops Zwift's product 0, where
   `file_id` and `device_info` are read. `device_info_test` has the
   Zwift ride's bytes, and fails against the old code.
7. Found by Jan on a Wahoo ELEMNT ROAM ride: its barometer, temperature
   sensor, GPS and accelerometer were listed as sensors; its BLE heart
   rate strap had no type; the ROAM itself showed as "Wahoo (product
   47)"; and the sensors' names repeated their types ("Garmin Radar
   radar"). **Fixed**, each in its own commit with a test: a listing
   without a source type needs a serial number of its own to be a
   sensor; a BLE type that isn't one falls back to ANT+'s; the device's
   name comes from its own `device_info` listing when `file_id` has
   none; and a sensor's product name is only shown for an unknown type.

No bugs found against the spec's Version 1 scope.
