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
`cycling-dashboard`, with a power gauge and a heart rate graph, are a good
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

The overlay's font is [Inter](https://rsms.me/inter/), by The Inter
Project Authors, under the SIL Open Font License 1.1
(`share/fonts/Inter-OFL.txt`).

The map tiles are from [OpenStreetMap](https://www.openstreetmap.org),
© OpenStreetMap contributors.
