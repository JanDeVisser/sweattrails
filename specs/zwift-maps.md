# Zwift maps

Status: In progress, on branch feature/zwift-maps

## Goal

A ride in Zwift shows on a map of the Zwift world it was ridden in,
rather than on OpenStreetMap, where most of it is open sea or roads the
ride never went down.

## Feature

### What there is now

- Zwift writes its FIT files with GPS positions, so the app treats a
  Zwift ride as any other ride: the activity map (`ActivityMap`, on
  `Map.h`) and the overlay's `map` element show the track on
  OpenStreetMap tiles (`specs/map-pan-zoom.md`,
  `specs/overlay-map-tiles.md`).
- Zwift places each world on the earth. Its fictional worlds, such as
  Watopia and the Makuri Islands, lie in the Pacific, so OpenStreetMap
  shows the track over open sea. Its worlds on real cities, such as
  London, New York and Richmond, lie on those cities, but their roads
  only roughly follow the real ones.
- The FIT reader reads `file_id.manufacturer`, which is `zwift` (260) for
  a Zwift file, but `Activity` doesn't keep it, so nothing after loading
  can tell a Zwift ride from another.
- Zwift files come into the store from Garmin Connect and Strava, the
  same as outdoor rides (`specs/garmin-download.md`).

### Which rides, and which map

- A ride gets a Zwift map when its FIT file was made by Zwift
  (`file_id.manufacturer` is `zwift`) and its track lies inside the
  bounds of one of Zwift's worlds. Both checks are needed: by bounds
  alone, an outdoor ride in London, Richmond or Paris would get Zwift's
  map, and by manufacturer alone the app wouldn't know which world.
- It's the activity map on the main screen that shows the Zwift world.
  The overlay's `map` element keeps OpenStreetMap *(deferred)*: a Zwift
  video is usually a screen recording, which already shows the game's
  own minimap.

### Where the maps come from

- Zwift has an image of each world, used in its own apps, at
  `https://cdn.zwift.com/static/images/maps/MiniMap_<name>.png`, such as
  `MiniMap_Watopia_2.png` and `MiniMap_Japan.png` for the Makuri
  Islands. These aren't documented by Zwift and may change: the Watopia
  image was replaced by `MiniMap_Watopia_2.png` in 2023 when it fell
  behind the roads. The images are Zwift's copyright, so the app
  downloads them at run time, as it does OpenStreetMap's tiles, and
  doesn't ship them.
- Where each image lies on the earth, its north-west and south-east
  corners in latitude and longitude, comes from
  [zwift-data](https://github.com/andipaetzold/zwift-data) (MIT), the
  data behind ZwiftMap, which shows Zwift rides on Strava on these
  images. For example, Watopia is from -11.62597, 166.87747 to
  -11.74087, 167.03255. The worlds now are Watopia, Richmond, London, New
  York, Innsbruck, Bologna, Yorkshire, Crit City, the Makuri Islands,
  France, Paris and Scotland.
- The table of worlds is `share/zwift-worlds.json`, installed with the
  app: for each world its name, the image's URL, and its two corners.
  It's copied from zwift-data, which the README credits. When Zwift adds
  a world or replaces an image, that's an edit to the JSON and an
  install, not a rebuild.

  ```json
  [
    { "name": "Watopia",
      "image": "https://cdn.zwift.com/static/images/maps/MiniMap_Watopia_2.png",
      "north_west": [-11.62597, 166.87747],
      "south_east": [-11.74087, 167.03255] }
  ]
  ```

- A downloaded image is cached with the map tiles, and used from the
  cache from then on, as a tile is. A world whose image changes gets a
  new URL in the JSON, and so a new download.

### On the map

- The image is drawn under the track where its corners lie, in place of
  OpenStreetMap's tiles. The track, the laps, the selection and the
  marker are drawn over it as now, and zooming and panning work as now
  (`specs/map-pan-zoom.md`).
- Until the image has downloaded, the map shows OpenStreetMap, as now,
  and changes to the Zwift world when it's there. If the download fails,
  such as when Zwift has moved the image, the map stays on
  OpenStreetMap and the error is logged: a changed URL never leaves the
  map blank.
- The zoom levels are the same as with OpenStreetMap. Zoomed in far, the
  image is scaled up and soft, which is fine for a game world; how far
  that goes depends on the image's size, which the Developer finds out.
  Zoomed out past the world's edges, it lies on a plain background in
  the color of the image's sea, so that it doesn't look cut off.
- The map says "Map © Zwift", where it says "© OpenStreetMap
  contributors" for OpenStreetMap. The README credits Zwift for the
  images and zwift-data for the bounds.

### Version 1 scope

In: Zwift's rides, by manufacturer and bounds, on the activity map, on
Zwift's world image, downloaded and cached, placed by
`share/zwift-worlds.json`; OpenStreetMap until it's there or when it
fails; the same zoom and pan; "Map © Zwift"; the README.

*(Deferred)*:

- The overlay's `map` element on a Zwift world.
- Other games with worlds of their own, such as MyWhoosh. The world
  table could take them, with a way to match their files.
- Telling which route of a world the ride took.

## Open questions

None.

## Architecture

### The images are too big to draw as one

Zwift's images are large: Watopia's is 8192×6144, the Makuri Islands'
6144×6144, London's 4096×4096 (from their PNG headers). As one texture,
Watopia would take 200 MB of video memory. Its 8192 pixels span 0.155°
of longitude, which is about OSM zoom 16: one image pixel is about one
map pixel there.

So the app cuts a world's image into map tiles once, after downloading
it, and the activity map draws those as it draws OpenStreetMap's: the
zooming, panning, placeholders and unloading in `ActivityMap` and
`Map.h` stay as they are. Only where a tile comes from changes.

### The worlds (`src/App/Zwift.h`, `Zwift.cpp`)

```cpp
struct ZwiftWorld {
    std::string name;      // "Watopia"
    std::string image;     // The image's URL
    Coordinates north_west;
    Coordinates south_east;

    // The cache's name for it, from the image's file name, such as
    // "MiniMap_Watopia_2": a new image is a new URL, and so a new cache.
    [[nodiscard]] std::string cache_name() const;
    [[nodiscard]] Box         box() const;
    static Decoded<ZwiftWorld> decode(JSONValue const &);
};

// share/zwift-worlds.json, read at start. A world without its four
// fields is left out, with a warning; a file that doesn't parse leaves
// the app without Zwift maps.
Expected<std::vector<ZwiftWorld>> read_zwift_worlds(fs::path const &file);

// The world whose box holds the activity's track, if Zwift made it.
ZwiftWorld const *zwift_world(std::span<ZwiftWorld const> worlds, Activity const &activity);
```

`Activity` gains `std::optional<FIT::manufacturer> manufacturer`, from
the FIT `file_id` message in `load_fit_file_filter()`, beside `serial`
and `number`.

### Cutting the image into tiles

- **`world_tiles(world, image_width, image_height, zoom)`**, in
  `Zwift.cpp`: the tiles at a zoom that the world's image touches, each
  with the part of the image it shows and where that goes in the tile,
  both as rectangles. The image's corners are put on the map with
  `world_pixel()`, and the image is stretched between them: over a
  world 0.1° high, Web Mercator and a plain linear mapping differ by far
  less than a pixel. Pure arithmetic, so a test can check it.
- **The world's zoom levels**: from 8, where a world is a part of one
  tile, up to its **native zoom**, the first where an image pixel is no
  larger than a map pixel (16 for Watopia). Watopia is about 600 tiles
  at 16, 800 in all, some 20 MB on disk.
- **`prepare_zwift_world(storage, world)`**, on the job thread:
  1. Downloads the image to `tiles/zwift/<cache name>.png.part` with
     `download()` from `Map.cpp`, and renames it to `.png` when it's
     complete, as tiles are.
  2. Decodes it with raylib's `LoadImageFromMemory()`, which only uses
     the CPU, so it's safe on the job thread. 200 MB in memory for a
     moment, for Watopia.
  3. For each zoom, writes each tile from `world_tiles()` to
     `tiles/zwift/<cache name>/<zoom>/<x>/<y>.png`: the image's part,
     scaled to its place with `ImageResize()`, on the image's background
     where the tile goes past the image's edge.
  4. Last, writes `tiles/zwift/<cache name>/world.json` with the native
     zoom and the background color, the image's top left pixel (the
     sea). That file is what says the tiles are all there: a cut that
     was interrupted, by quitting, is done again next time.
- A new job, **`JobType::PrepareZwiftWorld`**, runs it, and a new
  notification, **`ZwiftWorldReady { cache_name, error }`**, says it's
  done. A failure is logged with `warning()`, and the map stays on
  OpenStreetMap. A world that failed isn't tried again until the app
  restarts.

### The map's source (`Map.h`, `ActivityMap`)

```cpp
// Where the activity map's tiles come from.
struct MapSource {
    fs::path    dir;           // The tiles' cache: tiles_dir, or a world's
    char const *attribution;   // OsmAttribution or "Map © Zwift"
    bool        downloads;     // OSM's are downloaded; a world's are all there
    u8          max_zoom;      // MaxMapZoom, or the world's native zoom
    Color       background;    // LIGHTGRAY, or the world's sea
};
```

- `Tile::get_cached_tile()` takes the directory instead of the
  storage, so it reads either cache.
- `ActivityMap::set_activity()` starts on OpenStreetMap. For a Zwift
  ride in a known world, it uses the world's tiles if its `world.json`
  is there, and otherwise schedules `PrepareZwiftWorld` (once per world
  at a time), and changes to the world when `ZwiftWorldReady` comes for
  it and the ride is still on display: it unloads the OSM tiles and
  loads the view again.
- With a world's source, `view_changed()` doesn't download: a tile
  that isn't cached is past the world's edge, and is drawn in the
  background color. Above the native zoom, it loads the native tile
  that holds the one wanted, so that `cover()` draws it scaled up, as it
  draws a placeholder now.
- `draw_attribution()` draws the source's attribution.

### Settings and files

- `share/zwift-worlds.json`, as in the Feature section, with the twelve
  worlds of zwift-data, installed with `share/`.
- `SweatTrails` reads it at start into `zwift_worlds`, a
  `std::vector<ZwiftWorld>`, and logs a warning if it can't.
- The README credits Zwift's images and zwift-data's bounds.

## Test plan

- `ZwiftTest` (`zwift_test`):
  - `ZwiftWorld::decode()` and `read_zwift_worlds()`: the shipped file
    reads, with twelve worlds; a world without a corner is left out;
    a file that isn't JSON is an error.
  - `cache_name()` from URLs with and without a query or `_2`.
  - `zwift_world()`: a Zwift ride in Watopia finds it; the same track
    from another manufacturer doesn't; a Zwift ride in London finds
    London; a track that goes past a world's edge, or none at all, finds
    nothing.
  - `world_tiles()`: the native zoom of Watopia's image is 16; the tiles
    at a zoom cover the image's box and no more; each image part and its
    place in the tile are in proportion; an edge tile shows only part of
    the image; over all tiles at a zoom, the image parts add up to the
    whole image.
- `ActivityLoadTest`: a FIT file built with `FITBuilder` (`TestFit.h`)
  with manufacturer 260 loads as Zwift's, and one without a
  manufacturer, or another, doesn't.
- `prepare_zwift_world()` with a small image already in place of the
  download, in a temporary store: the tiles and `world.json` are
  written, and a tile past the image's edge has the background. No
  network: the download is skipped when the `.png` is there.
- `MapTest`: `get_cached_tile()` from a directory.
- By hand: a Watopia ride starts on OpenStreetMap and changes to
  Watopia; the second time it's Watopia right away; zoomed in and out,
  panned; the attribution; a ride in London; an outdoor ride is
  unchanged; with the network off, the map stays on OpenStreetMap.

## Review notes

*Filled in by the Reviewer.*
