# Device info: the recording device and its sensors

Status: In progress, on branch feature/48-device-info

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
  "Garmin heart rate monitor (product 3865)". Under that, in the smaller
  font, its serial number, software version, and battery status at the
  end of the activity (ok, low, critical, ...). The battery is the most
  useful of these: it answers "why did the power drop out?". Product
  numbers aren't turned into names in this version (see Version 1
  scope).
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
  empty.

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
  local). A listing without a `source_type`, as in older files, counts as
  a sensor unless it has the creator's serial number, which is how those
  files mark the device's own parts.
- `Activity` gets `std::optional<u16> product` (from `file_id`) and
  `std::vector<Sensor> sensors`, filled on a deep load only, as the
  records and laps are.

### The box's text (`widget/ActivityInfo.h`, `.cpp`, new)

- `maker_name(std::optional<manufacturer>)`, taken out of
  `device_name()`, which keeps working as it does. Sensors use it too.
- `sensor_type_name(Sensor const &)`: "heart rate monitor", "power
  meter", "speed and cadence sensor", "radar", ... from the ANT+ or BLE
  type, and "sensor" for one it doesn't know.
- `describe(Activity const &, fs::path const &store)` returns an
  `ActivityInfo`: the file's path relative to the store (`fit_path()`,
  so a recording's is in its `.recordings` folder), the recording
  device's line, and per sensor its line and detail line. A line is
  `{ text, small }`: the part in the normal font and the part in the
  smaller one ("Garmin heart rate monitor" + "(product 3865)"; the
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
  line, the path relative to the store, also for a recording.
  `reveal_command()` on both platforms' branches as compiled.
- `ActivityInfoBox` with `ScopedTestApp`: Esc closes, F calls reveal with
  the file, Tab and Shift+Tab call the switch with +1 and -1 and describe
  again.
- `DataFieldsTest`'s `device_name()` tests keep passing after the split.
- By hand: the drawing, the layout in both fonts, the click on the hint,
  Finder opening on the file, and Tab in the box on an activity of two
  recordings; and the real Garmin ride from 2026-11-13 showing its
  sensors.

## Review notes

*Filled in by the Reviewer.*
