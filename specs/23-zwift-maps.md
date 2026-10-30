# Zwift maps

Status: Done

## Goal

A ride in Zwift shows on a map of the Zwift world it was ridden in,
rather than on OpenStreetMap, where most of it is open sea or roads the
ride never went down.

## Feature

### What there is now

- Zwift writes its FIT files with GPS positions, so the app treats a
  Zwift ride as any other ride: the activity map (`ActivityMap`, on
  `Map.h`) and the overlay's `map` element show the track on
  OpenStreetMap tiles (`specs/10-map-pan-zoom.md`,
  `specs/17-overlay-map-tiles.md`).
- Zwift places each world on the earth. Its fictional worlds, such as
  Watopia and the Makuri Islands, lie in the Pacific, so OpenStreetMap
  shows the track over open sea. Its worlds on real cities, such as
  London, New York and Richmond, lie on those cities, but their roads
  only roughly follow the real ones.
- The FIT reader reads `file_id.manufacturer`, which is `zwift` (260) for
  a Zwift file, but `Activity` doesn't keep it, so nothing after loading
  can tell a Zwift ride from another.
- Zwift files come into the store from Garmin Connect and Strava, the
  same as outdoor rides (`specs/07-garmin-download.md`).

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
  (`specs/10-map-pan-zoom.md`).
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

- **`world_area(world, zoom)`**, in `Zwift.cpp`: where the world lies
  on the map at a zoom, in whole map pixels, from its corners'
  `world_pixel()`s rounded. The image is scaled to that size to be cut
  into the zoom's tiles, one image pixel to a map pixel: stretched
  between its corners, which over a world 0.1° high is Web Mercator to
  far less than a pixel.
- **`world_tiles(world, zoom)`**: the tiles the world's area touches,
  each with the part of the scaled image it shows and where that goes
  in the tile, as rectangles of whole pixels and of the same size, so
  that each of the image's pixels is in one tile (review note 1). Pure
  arithmetic, so a test can check it.
- **The world's zoom levels**: from 8 (`MinWorldZoom`), where a world is
  a part of one tile, up to its **native zoom**, `native_zoom()`, the
  zoom nearest the image's resolution (16 for Watopia, where an image
  pixel is 0.88 of a map pixel), but at least `MinNativeZoom`, 15: the
  highest zoom, 18, less `CoverLevels`, how far up `cover()` looks for a
  tile to scale up. A smaller image is scaled up to be cut there, so
  that it shows at every zoom (review note 2). The twelve worlds'
  images are all 15 or 16. Cut from Zwift's image, Watopia is 905
  tiles, 21 MB, in 4.4 seconds with the download.
- **`prepare_zwift_world(storage, world)`**, on the job thread:
  1. Downloads the image to `tiles/zwift/<cache name>.png` with
     `download_file()`, which `Tile::get_tile()` now uses too: to a
     `.part` file first, renamed when it's complete. An image that's
     there isn't downloaded again; one that doesn't decode is removed.
     A tile's download has 30 seconds in all; an image's goes on as
     long as it comes at 1 KB/s or more (`DownloadLimit::Stall`, review
     note 4).
  2. Decodes it with raylib's `LoadImage()`, which only uses the CPU, so
     it's safe on the job thread. Converting it to RGBA and scaling it
     to the native zoom each hold two copies for a moment: some 360 MB
     at most, for Watopia (review note 5).
  3. From the native zoom down, scales the whole image to the zoom's
     `world_area()`, half the one before, and writes each tile from
     `world_tiles()` to `tiles/zwift/<cache name>/<zoom>/<x>/<y>.png`:
     the image's part, on the background where the tile goes past the
     image's edge. That's quicker than scaling each tile from the
     8192-pixel original.
  4. Last, writes `tiles/zwift/<cache name>/world.json` with the native
     zoom and the background color, the image's top left pixel (the
     sea). That file is what says the tiles are all there: a cut that
     was interrupted, by quitting, is done again next time.
- A new job, **`JobType::PrepareZwiftWorld`**, runs it, and a new
  notification, **`ZwiftWorldReady { cache_name, map, error }`**, says
  it's done. `SweatTrails::prepare_zwift_world()` schedules it, unless
  the world is in `zwift_preparing` or `zwift_failed`. A failure is
  logged with `warning()`, and the map stays on OpenStreetMap. A world
  that failed isn't tried again until the app restarts.
- `cut_world()` reads `world.json`, and gives nothing without it, or
  with a zoom or a color it can't use.

### The map's source (`Map.h`, `ActivityMap`)

```cpp
// Where the activity map's tiles come from.
struct MapSource {
    fs::path    dir;           // The tiles' cache: tiles_dir, or a world's
    char const *attribution;   // OsmAttribution or ZwiftAttribution
    bool        downloads;     // OSM's are downloaded; a world's are all there
    u8          max_zoom;      // MaxMapZoom, or the world's native zoom
    Color       background;    // LIGHTGRAY, or the world's sea
};
```

`world_source(WorldMap)` in `Zwift.h` makes a world's.

- `Tile::get_cached_tile()` takes the directory instead of the
  storage, so it reads either cache.
- `ActivityMap::set_activity()` starts on OpenStreetMap. For a Zwift
  ride in a known world, it uses the world's tiles if its `world.json`
  is there, and otherwise keeps the world in `world_to_cut`.
  `view_changed()` asks for the view's OSM tiles first, and then
  schedules `PrepareZwiftWorld` (once per world at a time): there's one
  job thread, and the cut takes seconds, which would keep the tiles
  waiting (review note 3). It changes to the world when
  `ZwiftWorldReady` comes for it and the ride is still on display: it
  unloads the OSM tiles and loads the view again.
- With a world's source, `view_changed()` doesn't download: a tile
  that isn't cached is past the world's edge, and is drawn in the
  background color. It goes with the failed tiles, so that it isn't
  looked for on disk again on every step of a drag (review note 6). Above the native zoom, it loads the native tile
  that holds the one wanted, `Tile::ancestor()`, so that `cover()`
  draws it scaled up, as it draws a placeholder now.
- `ActivityMap::world_ready()` changes the source with
  `change_source()`: it drops the downloads and tiles of the one before,
  and takes a new `map_id`, so that tiles still arriving for
  OpenStreetMap are dropped.
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

1. **Bug: an image column is lost at tile seams, and each tile is
   resampled.** `write_tile()` (`src/App/Zwift.cpp:172`) passes
   `world_tiles()`'s float source rectangle to `ImageFromImage()`, which
   truncates its x and its width separately. When the image's width at a
   zoom rounds down (`Zwift.cpp:201`), a whole tile's source is just under
   256 pixels wide. For example, Watopia at zoom 13 is 903.4 map pixels
   wide, and so its image is 903 pixels wide. Tile 7894 then has source x
   156.2 and width 255.89, which copies columns 156 to 410. Tile 7895
   starts at 412.09, which copies from 412. Column 411 is in no tile, and
   each tile's 255 columns are stretched to 256 by `ImageResize()`. This
   happens at every seam, in about half of the zooms (those whose width
   rounds down), for every world: a road or coast that runs north to south
   along a seam has a one-pixel gap, and all the tiles are a little
   blurred. Because the image is already scaled to the zoom's size, the
   tiles could be cut at whole-pixel offsets, with tile pixel `px` taken
   from image column `px + 256 * x - lround(nw.x)`, and with no resize for
   each tile. Test: `test_no_column_is_lost_at_a_seam` (`test/ZwiftTest.cpp:316`)
   fails.
2. **Bug: above native zoom + 3, a world's map is blank.** With a world's
   source, `view_changed()` (`src/App/widget/ActivityMap.cpp:176-181`)
   only loads the tile at the native zoom, and `draw_tiles()` draws it
   through `cover()`, which looks at most three levels up
   (`src/App/Map.cpp:414`). `max_zoom()` lets the user zoom to 18
   (`MaxMapZoom`), so a world whose native zoom is 14 or less is plain
   background, with the track over it, at the highest zooms.
   `native_zoom()` makes 14 of Watopia's corners with a 2048-pixel image,
   and can go down to 8. Watopia, the Makuri Islands and London (16) are
   fine, but the spec only gives the sizes of those three images.
   `view_changed()` could draw the native tile itself, scaled by
   `1 << (zoom - native)`, or `cover()` could take how far up to look. Test:
   `test_native_tile_covers_up_to_the_highest_zoom`
   (`test/ZwiftTest.cpp:398`) fails.
3. **Bug: while a world is cut, no OpenStreetMap tiles arrive, and nothing
   else runs on the job thread.** The spec says the map shows
   OpenStreetMap until the world is there. But `choose_source()` schedules
   `PrepareZwiftWorld` (`ActivityMap.cpp:97`, through
   `SweatTrails.cpp:961`) before the first `view_changed()` schedules
   `DownloadTiles`, and there's one job thread (`AppBus::handler`,
   `src/Lib/Widget/Widget.h:303`). So OSM tiles that aren't cached only come
   in after the download and the cut: the map is grey and then changes to
   the world. The same goes for tiles for another activity shown during
   the cut, the overlay preview's tiles, and a Garmin or Wahoo download
   the user starts. Each of those waits seconds, or longer on a slow
   network. It's least visible for Watopia, whose OSM map is sea, but it
   doesn't match the spec. A second thread, or cutting a zoom at a time
   in its own jobs, would avoid it. Not tested: it needs the app's job
   bus.
4. **Suggestion: the 30-second curl timeout covers the whole image.**
   `download()` sets `CURLOPT_TIMEOUT` to 30 s (`src/App/Map.cpp:122`),
   which was chosen for 256-pixel tiles. An 8192×6144 PNG of many
   megabytes on a slow connection can go over it. The world is then in
   `zwift_failed` until the app restarts, and the next session tries the
   whole download again and fails the same way. A longer timeout for
   `download_file()` of a world, or `CURLOPT_LOW_SPEED_LIMIT` in place of
   the total timeout, would fix it.
5. **Suggestion: the cut needs about twice the memory the spec says.**
   `LoadImage()` and then `ImageFormat()` to RGBA (`Zwift.cpp:256-262`)
   hold the RGB and RGBA copies at once, if the PNG has no alpha channel.
   The first `ImageResize()` (`Zwift.cpp:201`, from 8192×6144 to about
   7240×5430 for Watopia) holds the original and the result. That's about
   360 MB at its peak, not 200 MB. That's acceptable on a desktop, but the
   spec's figure should say so.
6. **Suggestion: tiles past a world's edge are looked up on every view
   change.** For a world's source, a tile that isn't cached isn't put in
   `failed` (`ActivityMap.cpp:183`), so each drag or wheel step runs
   `fs::exists` again for every tile past the edge. Putting them in
   `failed`, as the ancestor branch above it does, would stop that. The
   same code means that a `world.json` whose tiles were deleted by hand
   shows only the background, and never recovers. That's the design, but
   a missing tile at a zoom where `world_tiles()` says there should be
   one could also be taken as a reason to cut again.
7. **Missing test (added, passes): an interrupted cut is done again.**
   With tiles there but no `world.json`, `cut_world()` gives nothing, and
   `prepare_zwift_world()` cuts again and writes it:
   `test_interrupted_cut_is_done_again` (`test/ZwiftTest.cpp:383`).
8. **Missing test (added, passes): world corners on tile edges.** A
   south-east corner exactly on a tile edge gives no extra empty column
   of tiles: `test_world_tiles_on_tile_edges` (`test/ZwiftTest.cpp:416`).
9. **Missing test (added, passes): FIT's invalid manufacturer.** A
   `file_id` with manufacturer 0xFFFF loads as no manufacturer:
   `test_invalid_manufacturer` (`test/ActivityLoadTest.cpp:119`). Reading
   `file_id.manufacturer` as `uint16` is right: with `enum_`, the reader
   took the low byte of the union, which is 4 for Zwift's 260. Nothing
   else uses `file_id.manufacturer`, and `fitexplore` prints fields
   generically, so the change doesn't affect anything else.

Checked and found fine: the job only reads `storage.tiles_dir` through
`SweatTrails::the()`, as `job_download_tiles` does. `change_source()`
takes a new `map_id`, so OSM tiles still arriving are dropped.
`waiting_for_world` is reset when another activity is shown, and showing
the ride again while its world is being cut waits for the same job.
`world.json` is written last. Downloads go to a `.part` file. Only raylib's
CPU image functions run on the job thread. The README credits Zwift and
zwift-data.

### Resolution

1. Fixed: `world_area()` and `world_tiles()` lay the image, scaled to
   each zoom's size, one image pixel to a map pixel, in whole pixels;
   each tile's part is copied without scaling.
   `test_no_column_is_lost_at_a_seam` gives each column its own color
   and finds each where it lies on the map. The review's version of it
   failed on the old cut; this one passes. The other tile tests now
   check whole pixels, and that each pixel is in one tile.
2. Fixed: `native_zoom()` is at least `MinNativeZoom`, 15, which is
   `MaxMapZoom` less `CoverLevels`, now named in `Map.h`.
   `test_native_tile_covers_up_to_the_highest_zoom` failed with the
   old native zoom of 14 for a 2048-pixel Watopia, and passes. None of
   the twelve worlds' images was affected: they're all 15 or 16.
3. Fixed: the map asks for the cut after the view's OSM tiles. Not
   tested: it needs the app's job queue.
4. Fixed: `DownloadLimit::Stall` for a world's image. No test without
   the network.
5. The Architecture says some 360 MB, for a moment.
6. Fixed, the first part: a world's missing tiles go with the failed
   ones. A `world.json` whose tiles were deleted isn't handled:
   deleting `world.json` too has the world cut again.
7. to 9. The review's tests are committed, and pass.
