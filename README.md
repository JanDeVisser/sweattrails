# SweatTrails

A desktop app for exploring sports activities: rides, runs, walks and
more, from FIT files, Strava and Rungap. It shows an activity on a map,
with its data and graphs. It's written in C++26, on
[raylib](https://www.raylib.com).

## Building

```sh
cmake -B build -G Ninja
ninja -C build
ctest --test-dir build --output-on-failure
```

How the app is developed, from specs, is in `CLAUDE.md`; the coding style
is in `STYLE.md`, and how to write tests in `test/README.md`.

## Credits

The sport icons are by [Icons8](https://icons8.com).

The map tiles are from [OpenStreetMap](https://www.openstreetmap.org),
© OpenStreetMap contributors.
