# Sport Icons for Walking and Hiking, and an Application Icon

Status: In review, on branch feature/icons

## Goal

- A walking and a hiking icon for the activity header, in the style of the
  sport icons there are now.
- An application icon, which the app never had.

## How it works now

### Sport icons

- The icons in `share/icons` are from Icons8 (https://icons8.com), in their
  Color style, with skin type 2 (`icons8-cycling-skin-type-2-96.png`), in
  48 and 96 pixels. Icons8's free license needs a link to Icons8 wherever
  the icons are used. The app and the repository don't have one.
- Icons8 has Walking Skin Type 2 and Trekking Skin Type 2 in that style.
  Icons8 doesn't let tools download from it, so Jan downloads them.
- `ActivityDataDisplay::set_activity()` picks the icon by sport: cycling,
  running, and a default. The icons for swimming, mountain biking, the
  treadmill, triathlon and windsurfing are there, but not used.
- The default is broken: its name starts with `share/icons/` and then gets
  `<datadir>/icons/` put in front, so the icon isn't found, and every
  sport besides cycling and running shows no icon.

### Application icon

- `App::create()` loads `icon_file` (`"sweattrails.png"`, a path relative
  to the working directory, and the file doesn't exist) and calls
  `SetWindowIcon()`.
- On macOS that does nothing: windows have no icons there. The Dock and
  Finder show the icon of the `.app` bundle, an `.icns` file named in its
  `Info.plist`. CMake builds the app as a bundle (`MACOSX_BUNDLE`), but
  gives it no icon.
- On Linux and Windows, `SetWindowIcon()` does set the window's icon.

## Feature

### Sport icons

- Walking (`sport::Walking`) shows Walking Skin Type 2, and hiking
  (`sport::Hiking`) Trekking Skin Type 2. Jan downloads them from Icons8
  in 48 and 96 pixels, as `icons8-walking-skin-type-2-<size>.png` and
  `icons8-trekking-skin-type-2-<size>.png` in `share/icons`, before the
  Developer starts.
- The icons that are there are used too: swimming, mountain biking
  (cycling's `mountain` sub-sport), the treadmill (running's `treadmill`
  sub-sport), triathlon for `sport::Multisport`, which is how FIT records
  a triathlon (FIT has no triathlon sport), and windsurfing for
  `sport::Windsurfing`. `Activity` doesn't keep the sub-sport now; it has
  to be read from the session message, which has it.
- The default works again: other sports show the "hyperactive" icon.
- The README credits Icons8, with a link, as the license asks.

### Application icon

- A new icon, drawn for SweatTrails: a blue sweat drop, with a red route,
  like the map's track, winding through it. It says "sweat" and "trails", and a simple shape like that stays
  readable at 16 pixels in the Dock and in Finder, where a route on a map
  tile turns to noise. (An "ST" monogram was the other option; it says
  little about the app.)
- It's kept as an SVG, its source, in `share/icons`, with the PNG and the
  `.icns` made from it.
- macOS: the bundle gets the `.icns`, so the Dock and Finder show it.
- Linux and Windows: `SetWindowIcon()` gets the PNG, found in the app's
  data directory rather than relative to the working directory.

### Version 1 scope

Everything above.

## Open questions

None.

## Architecture

**The sub-sport.** `Activity` gets `std::optional<FIT::sub_sport>
sub_sport`, copied from the session message in `load_fit_file_filter()`,
as `sport` is. The FIT `session` struct already reads it (field 6).

**Picking the icon** moves out of `ActivityDataDisplay::set_activity()`
into a free function, so a test can reach it without a window, in a new
`src/App/widget/SportIcon.h`:

```cpp
// The file name, in <datadir>/icons, of the 96 pixel icon for a sport.
std::string_view sport_icon(sport the_sport, std::optional<FIT::sub_sport> the_sub_sport);
```

| Sport | Sub-sport | Icon |
|---|---|---|
| Cycling, E_biking | `mountain`, `e_bike_mountain` | cycling-mountain-bike |
| Cycling, E_biking | others | cycling |
| Running | `treadmill` | treadmill |
| Running | others | running |
| Walking | | walking |
| Hiking | | trekking |
| Swimming | | swimming |
| Multisport | | triathlon |
| Windsurfing | | windsurfing |
| anything else | | hyperactive |

E-biking gets the cycling icons, as it counts as a ride for the data
fields. `set_activity()` loads `SWEATTRAILS_DATADIR "/icons/"` plus the
name.

**The application icon** is `share/icons/sweattrails.svg`, drawn by hand.
`share/icons/make-app-icon.sh` makes `sweattrails.png` (256 pixels) and
`sweattrails.icns` from it, using a small Swift script next to it
(`svg2png.swift`, which renders with AppKit and keeps the transparency;
`qlmanage` doesn't) and macOS's `iconutil`. The PNG and the `.icns` are
committed, so a build needs neither: the script only runs when the SVG
changes, and needs a Mac.

- macOS: CMake adds `sweattrails.icns` to the bundle's `Resources`, and
  sets the target's `MACOSX_BUNDLE_ICON_FILE`, which goes into the
  generated `Info.plist`.
- `App::icon_file` defaults to empty, meaning no window icon, rather than
  a file that doesn't exist. `App::create()` skips `SetWindowIcon()` when
  it's empty, and unloads the image after setting it (it leaked).
  `SweatTrails` sets it to `SWEATTRAILS_DATADIR "/icons/sweattrails.png"`,
  except on macOS, where windows have no icons.

**The credit.** There's no README in the repository, so this adds a short
`README.md`: what SweatTrails is, how to build it, and the credit to
Icons8, with a link. It credits OpenStreetMap for the map tiles too, which
its tile usage policy asks for.

`fit/Profile.h` had no `#pragma once`, and `SportIcon.h` is the first
header to include it next to another that does; it gets one.

## Test plan

In a new `test/SportIconTest.cpp`:

- `sport_icon()` for each row of the table, including the sub-sports, and
  an unlisted sport getting hyperactive.
- Every icon `sport_icon()` can return exists in `share/icons` in the
  source tree (the test gets the path as a compile definition). That
  catches the broken default, and a typo in a name.
- `share/icons/sweattrails.png` and `sweattrails.icns` exist.

In `test/ActivityLoadTest.cpp`: a session with a sub-sport loads it into
`Activity::sub_sport`, and one without leaves it empty.

By hand: the icons in the header for a ride, a run, a walk and a hike; the
app icon in the Dock and Finder; and that the icon reads at 16 pixels.

## Review notes

*Filled in by the Reviewer.*
