# SweatTrails

A desktop app for exploring sports activities: rides, runs, walks and
more, from FIT files, Strava and Rungap. It shows an activity on a map,
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
| libcurl | map tiles, Garmin Connect | the system's | your distribution |
| zlib | unzipping Garmin Connect downloads | the system's | your distribution |
| iconv | text encodings | the system's | glibc's |

### macOS

The Xcode Command Line Tools, for Clang, libc++, libcurl, zlib and iconv,
and from [Homebrew](https://brew.sh):

```sh
brew install cmake ninja pkgconf raylib freetype llvm
```

`llvm` is for its clang-format, which the pre-commit hook uses (Apple's
formats differently); the build itself uses Apple's Clang. Regenerating
the app icon (`share/icons/make-app-icon.sh`) uses Swift and `iconutil`,
which come with the Command Line Tools.

### Linux

Not tested yet. On Debian or Ubuntu, the packages would be:

```sh
sudo apt install clang libc++-dev libc++abi-dev cmake ninja-build pkg-config \
    libraylib-dev libfreetype-dev libcurl4-openssl-dev zlib1g-dev
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

How the app is developed, from specs, is in `CLAUDE.md`; the coding style
is in `STYLE.md`, and how to write tests in `test/README.md`.

## Credits

The sport icons are by [Icons8](https://icons8.com).

The map tiles are from [OpenStreetMap](https://www.openstreetmap.org),
© OpenStreetMap contributors.
