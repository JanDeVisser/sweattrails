# SweatTrails

A desktop app for exploring sports activities: rides, runs, walks and
more, from FIT files, Garmin Connect and Wahoo. It shows an activity on a map,
with its data and graphs. It's written in C++26, on
[raylib](https://www.raylib.com).

## Requirements

SweatTrails builds with Clang and libc++, for C++26: the build links
against libc++ whatever the platform's default. It needs CMake 3.27 or
later, Ninja, and these libraries:

| Library | Used for | macOS | Linux |
|---|---|---|---|
| [raylib](https://www.raylib.com) | windows, drawing, input | Homebrew | your distribution, or built from source |
| FreeType | fonts | Homebrew | your distribution |
| libcurl | map tiles, Garmin Connect, Wahoo | the system's | your distribution |
| zlib | unzipping Garmin Connect downloads | the system's | your distribution |
| OpenSSL 3 or later | the Wahoo login | Homebrew | your distribution |
| iconv | text encodings | the system's | glibc's |
| OpenGL | reading back the overlay's frames | the system's | your distribution |

Rendering the overlay to video runs [ffmpeg](https://ffmpeg.org), which
the app looks for on the `PATH`, and in `/opt/homebrew/bin` and
`/usr/local/bin`. It's not needed to build.

### macOS

The Xcode Command Line Tools, for Clang, libc++, libcurl, zlib and iconv,
and from [Homebrew](https://brew.sh):

```sh
brew install cmake ninja pkgconf raylib freetype openssl llvm
brew install ffmpeg   # for rendering the overlay to video
```

`llvm` is for its clang-format, which the pre-commit hook uses (Apple's
formats differently); the build itself uses Apple's Clang. Regenerating
the app icon (`share/icons/make-app-icon.sh`) uses Swift and `iconutil`,
which come with the Command Line Tools.

### Linux

Not tested yet. On Debian or Ubuntu, the packages would be:

```sh
sudo apt install clang libc++-dev libc++abi-dev cmake ninja-build pkg-config \
    libraylib-dev libfreetype-dev libcurl4-openssl-dev zlib1g-dev libssl-dev \
    libgl-dev ffmpeg
```

Distributions that don't package raylib need it built from
[source](https://github.com/raysan5/raylib).

### Windows

Not supported: running processes and sockets use POSIX APIs (`fork`,
`waitpid`, BSD sockets).

## Building

```sh
cmake -B build -G Ninja
ninja -C build
ctest --test-dir build --output-on-failure
ninja -C build install
```

The app reads its icons, fonts, themes and settings from `build/share`,
which only the install step fills: run it after changing anything in
`share`, such as an icon. The app is then `build/sweattrails.app` on
macOS, and `build/sweattrails` elsewhere.

The app starts in a maximized window on the monitor it was last on.
That has been tried on macOS, not yet on Linux. Under Wayland, an app
can't place its own window, so it may not start on the last monitor, and
glfw warns about that when it starts; X11 doesn't have this problem.

## Choosing an activity

SweatTrails starts with the newest activity. Left and Right show the one
before or after it, also in another month. Up and Down go through its
laps, and Escape back to the whole activity.

Ctrl+M lists the activities of the month of the one on display, oldest
first, with each one's sport, day and time, title, distance and moving
time. Up and Down select one, typing searches the titles, Enter shows
the one selected, and Escape closes the list. Left and Right go to the
month before or after that has activities, and Shift+Left and
Shift+Right a year back or on.

## Garmin Connect

SweatTrails downloads your activities from Garmin Connect with Garmin's
Personal API, which is free for your own data:

1. Create a key on the Garmin Connect website, under Settings, Personal
   API (https://connect.garmin.com/app/settings/personalApiToken). It's
   shown only once, and lasts from 30 days to a year: pick a year.
2. Save it where Garmin's own tools look for it, readable only by you:

   ```sh
   mkdir -p ~/.config/garmin
   pbpaste > ~/.config/garmin/connect-personal-api-key   # with the key copied
   chmod 600 ~/.config/garmin/connect-personal-api-key
   ```

   Or set `GARMIN_PERSONAL_API_KEY`, which wins over the file.

SweatTrails then downloads new activities when it starts, and on Ctrl+G.
It looks at the last 30 days; `"garmin": { "days": 365 }` in your
`settings.json` (in `~/Library/Application Support/sweattrails` on macOS,
`~/.config/sweattrails` on Linux, SweatTrails' config folder) looks
further back. Activities it already has aren't downloaded again. The name
an activity has on Garmin Connect becomes its title. When the key
expires, SweatTrails says so: create a new one the same way.

## Wahoo

SweatTrails downloads your workouts from Wahoo, such as an ELEMNT's rides,
with Wahoo's Cloud API. Wahoo has you register an app of your own for
that, and log in to it:

1. Register an app at https://developers.wahooligan.com, as a sandbox app,
   which works right away and is plenty for one person. Don't make it
   confidential: SweatTrails logs in without a client secret, with PKCE.
   Tick the scopes `workouts_read`, `user_read` and `offline_data`, all
   three: without one, Wahoo refuses the login, saying "The requested
   scope is invalid". Give it the redirect URI
   `https://localhost:8090/callback`.
2. Put its client ID in your `settings.json`, in SweatTrails' config
   folder:

   ```json
   { "wahoo": { "client_id": "..." } }
   ```

3. Wahoo only sends the browser back to an `https` address, so the login
   needs a certificate for localhost that your browser trusts. Make one
   with [mkcert](https://github.com/FiloSottile/mkcert) (`brew install
   mkcert` on macOS, `sudo apt install mkcert libnss3-tools` on Debian or
   Ubuntu; Firefox needs `nss` from Homebrew on macOS too):

   ```sh
   mkcert -install    # once: asks for your password
   cd ~/Library/Application\ Support/sweattrails    # ~/.config/sweattrails on Linux
   mkcert -cert-file localhost.pem -key-file localhost-key.pem localhost 127.0.0.1 ::1
   ```

   The certificate lasts a little over two years: make a new one the same
   way when it expires.
4. Press Ctrl+G. SweatTrails asks whether to log in to Wahoo: Yes opens
   the browser at Wahoo's login page. Once you've logged in and allowed
   SweatTrails access, the browser comes back to SweatTrails, which
   downloads your workouts. The login is saved in `wahoo-tokens.json` in
   the config folder, readable only by you.

SweatTrails then downloads new workouts when it starts, and on Ctrl+G,
along with Garmin Connect's. It looks at the last 30 days; `"wahoo": {
"days": 365 }` in the settings looks further back. The name a workout has
on Wahoo becomes its title. A ride recorded on a Wahoo and a Garmin at the
same time shows up twice. If you don't use SweatTrails for 60 days, Wahoo
forgets the login, and Ctrl+G asks you to log in again.

## Video overlay

SweatTrails draws an activity's data as a video overlay, to put over the
footage of the ride, with a transparent background. Ctrl+O previews it in
place of the data panel and the map, at the moment marked in the graph,
and Ctrl+Shift+O picks the layout. A layout is a JSON file that says
what's shown where; the built-in ones,
`share/overlays/cycling-basic.overlay.json`, `running-basic` and
`cycling-dashboard`, with a map, an elevation profile, a power gauge and a
heart rate graph, are a good
start for your own. Put those in the `overlays` folder in SweatTrails'
data folder, which the app makes when it starts
(`~/Library/Application Support/sweattrails/overlays` on macOS), named
`<name>.overlay.json`: the preview shows a change as soon as it's saved,
and one named like a built-in layout replaces it.
The format is in `specs/overlay-layout.md`.

A `text` element shows a template, such as `"{speed:.1f} {speed.unit}"`,
and a `value` element a field with its unit. The fields:

| Field | Shows |
|---|---|
| `speed`, `pace` | km/h or mph; min/km or min/mi, as `4:35` |
| `power`, `heart_rate`, `cadence` | W, bpm, rpm (spm for runs) |
| `distance`, `altitude` | km or mi; m or ft |
| `grade` | %, over the 30 m around the moment; a `value` can set `"window": 20`, from 10 to 200 m |
| `temperature` | °C or °F |
| `lr_balance` | left/right, as `48/52` |
| `elapsed` | `{elapsed}` is `1:05:09`, `{elapsed:ms}` `65:09` |
| `time_of_day` | `{time_of_day}` is `09:44`, `{time_of_day:hms}` `09:44:10` |
| `lap`, `title` | the lap, from 1; the activity's title |
| `date` | `{date}` is `14 Jun 2025`; `{date:%Y-%m-%d}` takes `strftime`-like specs |

During a pause, speed, power and cadence are 0, and heart rate is
missing. A number can change color with its value, in the unit shown:

```json
{ "type": "value", "field": "power",
  "color_by": { "thresholds": [[0, "#8ecae6"], [220, "#f9c74f"], [260, "#f94144"]] } }
```

A `gauge` shows a number as a `bar`, a `vbar` or an `arc`, and a `graph`
a field over the last `window` seconds or meters, or over the whole
activity with `"all"`, with a cursor at the moment. Their `min` and `max`
are the activity's lowest and highest unless they say. Any element can
have a panel behind it:

```json
{ "type": "gauge", "field": "power", "shape": "arc", "min": 0, "max": 400,
  "track": "#ffffff30", "fill": "#ffb703", "thickness": 14,
  "at": { "x": -40, "y": -40, "anchor": "bottom-right" }, "size": { "width": 200, "height": 200 } }
{ "type": "graph", "field": "heart_rate", "x_axis": "time", "window": 60,
  "line": { "color": "#f94144", "width": 3 }, "fill": "#f9414430", "cursor": { "radius": 6 },
  "background": "#00000060", "radius": 16, "padding": 16,
  "at": { "x": 40, "y": -200, "anchor": "bottom-left" }, "size": { "width": 400, "height": 100 } }
```

A `profile` shows the elevation of the whole ride, or with
`"extent": "lap"` of the lap, with a marker where the rider is, the part
done and the part remaining in their own colors, and a label by the
marker:

```json
{ "type": "profile", "extent": "all",
  "done": { "line": { "color": "#ffb703", "width": 3 }, "fill": "#ffb70360" },
  "remaining": { "line": { "color": "#ffffff", "width": 2 }, "fill": "#ffffff20" },
  "marker": { "radius": 7, "outline": "#ffb703", "drop_line": true },
  "marker_label": { "text": "{altitude:.0f} {altitude.unit} · {grade:+.1f}%", "font_size": 22 },
  "axis_labels": { "min_max": true }, "laps": { "color": "#ffffff60" },
  "at": { "x": "50%", "y": 40, "anchor": "top" }, "size": { "width": 900, "height": 120 } }
```

A `map` shows the track, the part ridden in one color and the rest in
another, with a marker where the rider is: the whole track with
`"view": "fit"`, or following the rider at an OpenStreetMap zoom level with
`"view": "follow"`. It's clipped to a `rect`, a `rounded` rectangle or a
`circle`, and so is its panel. With `tiles`, OpenStreetMap is under the
track, at an `opacity`, and with a `filter`: `none`, `grayscale`, `dark`,
or a color to tint it. OpenStreetMap's credit is then below the map, or
above it at the bottom of the frame, in the `attribution`'s style:

```json
{ "type": "map", "view": "follow", "zoom": 15, "clip": "circle",
  "background": "#00000060", "border": { "color": "#ffffff40", "width": 2 },
  "track": { "done": { "color": "#ffb703", "width": 5 }, "remaining": { "color": "#ffffff80", "width": 3 },
             "outline": { "color": "#00000080", "width": 2 } },
  "marker": { "color": "#ffffff", "radius": 8, "outline": "#ffb703" },
  "tiles": { "opacity": 0.6, "filter": "dark" }, "attribution": { "style": "label", "font_size": 14 },
  "at": { "x": -40, "y": 40, "anchor": "top-right" }, "size": { "width": 300, "height": 300 } }
```

OpenStreetMap doesn't allow fetching its tiles ahead of time, so a render
downloads each frame's tiles as it gets to them, and waits for them:
the first render of a ride takes longer, and the next ones read them from
the cache. A tile that can't be downloaded is left out.

An `image` shows a PNG, looked for next to the layout file and
then with the built-in layouts. It's never stretched: give it a `width`,
a `height`, both to fit it in that box, or neither for its own size, and
a `tint` to color it. An image's `size` replaces one from a style or the
defaults rather than merging with it. The app has white icons to tint in
`share/overlays/icons/`: `heart`, `bolt`, `speedometer`, `cadence` and
`mountain`:

```json
{ "type": "image", "src": "icons/heart.png", "tint": "#f94144",
  "at": { "x": 40, "y": -152, "anchor": "bottom-left" }, "size": { "width": 24 } }
```

Any element can show for part of the video only, with `visible`: `from`
and `to` are seconds from the video's start, or back from its end if
negative, and `fade` fades it in and out within that time. The dashboard
has a title card for its first 5 seconds; an outro could be:

```json
{ "type": "text", "text": "Thanks for watching", "visible": { "from": -8 }, "fade": 1,
  "at": { "x": "50%", "y": "50%", "anchor": "center" } }
```

The preview measures these times from the start of the graph's
selection, or of the whole activity, as a render's range starts.

A `group` is a panel with elements in it, its `children`, which are
placed in its box less its `padding`: their percentages, negative
coordinates and anchors are about that box. Moving the group moves them,
and while it shows they do, fading with it. It needs a `size`. Its
`font`, `font_size`, `color` and `shadow` come down to its children,
under their own styles; its panel stays its own. The `defaults` are its
children's too, apart from `visible` and `fade`, which are the group's. Groups can hold groups.
An error in a child names it as `element 3.2`:

```json
{ "type": "group", "background": "#00000060", "radius": 16, "font_size": 40,
  "at": { "x": 40, "y": 40 }, "size": { "width": 320, "height": 120 },
  "children": [
    { "type": "text", "text": "{distance:.1f} {distance.unit}", "at": { "x": 24, "y": 16 } },
    { "type": "text", "text": "{elapsed}", "at": { "x": 24, "y": -16, "anchor": "bottom-left" } } ] }
```

Ctrl+E renders the overlay to a ProRes 4444 movie with alpha, which Final
Cut and other editors import, for the part of the activity selected in the
graph, the lap, or the whole activity; the box it opens takes elapsed
times, `1:12:00-1:20:30`, or times of day, `@09:44:00-@09:52:30`. The
movie's timecode is the time of day, to line it up with the footage. It
goes to `~/Movies`, at 1080p and 29.97 fps, unless `settings.json` says
otherwise:

```json
"overlay": { "resolution": "4k", "fps": "60000/1001", "folder": "~/Footage" }
```

The resolutions are `720p`, `1080p`, `1440p` and `4k`, each with a
`-portrait` too, or `{ "width": 1080, "height": 1350 }`. ProRes 4444 is
big: about 2.5 GB a minute at 1080p. Ctrl+E during a render stops it.

How the app is developed, from specs, is in `CLAUDE.md`; the coding style
is in `STYLE.md`, and how to write tests in `test/README.md`.

## Credits

The sport icons are by [Icons8](https://icons8.com).

The overlay's icons, in `share/overlays/icons/`, are from
[Tabler Icons](https://tabler.io/icons), by Paweł Kuna, under the MIT
License (`share/overlays/icons/LICENSE`).

The overlay's font is [Inter](https://rsms.me/inter/), by The Inter
Project Authors, under the SIL Open Font License 1.1
(`share/fonts/Inter-OFL.txt`).

The map tiles are from [OpenStreetMap](https://www.openstreetmap.org),
© OpenStreetMap contributors.
